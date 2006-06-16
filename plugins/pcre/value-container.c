/*****
*
* Copyright (C) 2006 PreludeIDS Technologies. All Rights Reserved.
* Author: Yoann Vandoorselaere <yoann.v@prelude-ids.com>
*
* This file is part of the Prelude-LML program.
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by 
* the Free Software Foundation; either version 2, or (at your option)
* any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; see the file COPYING.  If not, write to
* the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
*
*****/

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <pcre.h>
#include <assert.h>

#include <libprelude/prelude.h>
#include <libprelude/prelude-string.h>
#include <libprelude/prelude-log.h>

#include "prelude-correlator.h"
#include "pcre-mod.h"
#include "value-container.h"


struct value_container {
        prelude_list_t list;
        prelude_list_t value_item_list;
        void *data;
};


typedef struct {
        prelude_list_t list;
        prelude_bool_t multiple_value;
        int refno;
        char *value;
} value_item_t;



static int add_dynamic_object_value(value_container_t *vcont,
                                    unsigned int reference, prelude_bool_t multiple)
{
        value_item_t *vitem;

        if ( reference >= MAX_REFERENCE_PER_RULE ) {
                prelude_log(PRELUDE_LOG_WARN, "reference number %d is too high.\n", reference);
                return -1;
        }

        vitem = malloc(sizeof(*vitem));
        if ( ! vitem ) {
                prelude_log(PRELUDE_LOG_ERR, "memory exhausted.\n");
                return -1;
        }
        
        vitem->value = NULL;
        vitem->refno = reference;
        vitem->multiple_value = multiple;
        
        prelude_list_add_tail(&vcont->value_item_list, &vitem->list);

        return 0;                
}



static int add_fixed_object_value(value_container_t *vcont, prelude_string_t *buf)
{
        int ret;
        value_item_t *vitem;

        vitem = malloc(sizeof(*vitem));
        if ( ! vitem ) {
                prelude_log(PRELUDE_LOG_ERR, "memory exhausted.\n");
                return -1;
        }

        ret = prelude_string_get_string_released(buf, &vitem->value);
        if ( ret < 0 ) {
                prelude_perror(ret, "error getting released string");
                free(vitem);
                return -1;
        }

        vitem->refno = -1;
        prelude_list_add_tail(&vcont->value_item_list, &vitem->list);

        return 0;
}



static int parse_value(value_container_t *vcont, const char *line)
{
        int i, ret;
        char num[10];
        const char *str;
        prelude_bool_t multiple;
        prelude_string_t *strbuf;

        str = line;

        while ( *str ) {
                if ( *str == '$' && *(str + 1) != '$' ) {
                                                
                        i = 0;
                        str++;

                        multiple = FALSE;
                        if ( *str == '*' ) {
                                str++;
                                multiple = TRUE;
                        }

                        while ( isdigit((int) *str) && i < sizeof(num) )
                                num[i++] = *str++;

                        if ( ! i )
                                return -1;

                        num[i] = 0;

                        if ( add_dynamic_object_value(vcont, atoi(num), multiple) < 0 )
                                return -1;

                        continue;
                }

                ret = prelude_string_new(&strbuf);
                if ( ret < 0 ) {
                        prelude_perror(ret, "error creating new prelude-string");
                        return -1;
                }

                while ( *str ) {
                        if ( *str == '$' ) {
                                if ( *(str + 1) == '$' )
                                        str++;
                                else
                                        break;
                        }

                        if ( prelude_string_ncat(strbuf, str, 1) < 0 )
                                return -1;
                        str++;
                }

                if ( add_fixed_object_value(vcont, strbuf) < 0 )
                        return -1;

                prelude_string_destroy(strbuf);
        }

        return 0;
}




static int propagate_string(prelude_list_t *outlist, const char *str)
{
        int ret;
        prelude_list_t *tmp;
        prelude_string_t *base;

        if ( prelude_list_is_empty(outlist) ) {
                ret = prelude_string_new_dup(&base, str);
                if ( ret < 0 )
                        return ret;
                
                prelude_linked_object_add_tail(outlist, (prelude_linked_object_t *) base);
                return 0;
        }
        
        prelude_list_for_each(outlist, tmp) {
                base = prelude_linked_object_get_object(tmp);

                ret = prelude_string_cat(base, str);
                if ( ret < 0 )
                        return ret;
        }

        return 0;
}



static int multidimensional_capture_to_flat_string(prelude_list_t *outlist,
                                                   value_item_t *vitem, capture_string_t *capture)
{
        int ret;
        unsigned int index, i;
        prelude_string_t *str;

        ret = prelude_string_new(&str);
        if ( ret < 0 )
                return ret;
        
        index = capture_string_get_index(capture);
        
        for ( i = 0; i < index; i++ ) {
                void *sub = capture_string_get_element(capture, i);
                
                if ( ! capture_string_is_element_string(capture, i) )
                        multidimensional_capture_to_flat_string(outlist, vitem, sub);
                else {
                        prelude_string_cat(str, sub);
                        
                        if ( i + 1 < index )
                                prelude_string_cat(str, ",");
                }
        }

        if ( ! prelude_string_is_empty(str) )
                propagate_string(outlist, prelude_string_get_string(str));
        
        prelude_string_destroy(str);

        return 0;
}



static inline void __list_splice(prelude_list_t *head, prelude_list_t *added)
{
        prelude_list_t *first = added->next;
        prelude_list_t *last = added->prev;
        prelude_list_t *at = head->next;

        first->prev = head;
        head->next = first;

        last->next = at;
        at->prev = last;
}


/**
 * list_splice - join two lists
 * @list: the new list to add.
 * @head: the place to add it in the first list.
 */
static inline void prelude_list_splice(prelude_list_t *head, prelude_list_t *added)
{
        if ( ! prelude_list_is_empty(added) )
                __list_splice(head, added);
}



static void multidimensional_capture_to_multiple_string(prelude_list_t *outlist,
                                                        value_item_t *vitem, capture_string_t *capture)
{
        unsigned int index, i;
        prelude_list_t *tmp, *bkp;
        prelude_string_t *str, *base;
        
        if ( prelude_list_is_empty(outlist) ) {                
                index = capture_string_get_index(capture);
                        
                for ( i = 0; i < index; i++ ) {
                        void *sub = capture_string_get_element(capture, i);
                        
                        if ( ! capture_string_is_element_string(capture, i) )
                                multidimensional_capture_to_multiple_string(outlist, vitem, sub);
                        else {
                                prelude_string_new_dup(&str, sub);
                                prelude_linked_object_add_tail(outlist, (prelude_linked_object_t *) str);
                        }
                }
        }
        
        else {
                prelude_list_t newlist;
                prelude_list_init(&newlist);
                                
                index = capture_string_get_index(capture);
                                
                prelude_list_for_each_safe(outlist, tmp, bkp) {
                        base = prelude_linked_object_get_object(tmp);
                        prelude_linked_object_del_init(base);
                        
                        for ( i = 0; i < index; i++ ) {
                                void *sub = capture_string_get_element(capture, i);
                                
                                if ( ! capture_string_is_element_string(capture, i) )        
                                        multidimensional_capture_to_multiple_string(outlist, vitem, sub);
                                else {                                        
                                        prelude_string_new_dup(&str, prelude_string_get_string(base));
                                        prelude_string_cat(str, sub);
                                        prelude_linked_object_add_tail(&newlist, (prelude_linked_object_t *) str);
                                }
                        }
                        
                        prelude_string_destroy(base);
                }

                prelude_list_splice(outlist, &newlist);
        }
}



static void resolve_referenced_value(prelude_list_t *outlist, value_item_t *vitem, capture_string_t *capture)
{
        unsigned int index;
         
        index = capture_string_get_index(capture);
        
        if ( (vitem->refno - 1) < 0 || (vitem->refno - 1) >= index ) {
                prelude_log(PRELUDE_LOG_ERR, "Invalid reference: %d (max is %u).\n", vitem->refno, index);
                return;
        }


        if ( ! capture_string_is_element_string(capture, vitem->refno - 1) ) {
                capture_string_t *sub = capture_string_get_element(capture, vitem->refno - 1);
                
                if ( vitem->multiple_value )
                        multidimensional_capture_to_multiple_string(outlist, vitem, sub);
                else
                        multidimensional_capture_to_flat_string(outlist, vitem, sub);
        } else
                propagate_string(outlist, capture_string_get_element(capture, vitem->refno - 1));
}



int value_container_resolve_listed(prelude_list_t *outlist, value_container_t *vcont,
                                   const pcre_rule_t *rule, capture_string_t *capture)
{
        prelude_list_t *tmp;
        value_item_t *vitem;

        prelude_list_init(outlist);
        
        prelude_list_for_each(&vcont->value_item_list, tmp) {
                vitem = prelude_list_entry(tmp, value_item_t, list);
                
                if ( vitem->refno != -1 )
                        resolve_referenced_value(outlist, vitem, capture);
                else
                        propagate_string(outlist, vitem->value);
        }
        
        
        return 0;
}



prelude_string_t *value_container_resolve(value_container_t *vcont,
                                          const pcre_rule_t *rule, capture_string_t *capture)
{
        int ret;
        prelude_list_t outlist, *tmp;
        prelude_string_t *str = NULL;
        
        prelude_list_init(&outlist);

        ret = value_container_resolve_listed(&outlist, vcont, rule, capture);
        if ( ret < 0 )
                return NULL;
        
        prelude_list_for_each(&outlist, tmp) {
                assert(str == NULL);
                str = prelude_linked_object_get_object(tmp);
        }
        
        return str;
}




int value_container_new(value_container_t **vcont, const char *str)
{
        int ret;
        
        *vcont = malloc(sizeof(**vcont));
        if ( ! *vcont ) {
                prelude_log(PRELUDE_LOG_ERR, "memory exhausted.\n");
                return -1;
        }

        (*vcont)->data = NULL;
        prelude_list_init(&(*vcont)->value_item_list);
        
        ret = parse_value(*vcont, str);
        if ( ret < 0 ) {
                free(*vcont);
                return ret;
        }

        return 0;
}



void value_container_destroy(value_container_t *vcont)
{
        value_item_t *vitem;
        prelude_list_t *tmp, *bkp;
        
        prelude_list_for_each_safe(&vcont->value_item_list, tmp, bkp) {
                vitem = prelude_list_entry(tmp, value_item_t, list);

                if ( vitem->value )
                        free(vitem->value);
                
                prelude_list_del(&vitem->list);
                free(vitem);
        }

        free(vcont);
}



void value_container_reset(value_container_t *vcont)
{
        value_item_t *vitem;
        prelude_list_t *tmp;
        
        prelude_list_for_each(&vcont->value_item_list, tmp) {
                vitem = prelude_list_entry(tmp, value_item_t, list);

                if ( vitem->refno != -1 && vitem->value ) {
                        free(vitem->value);
                        vitem->value = NULL;
                }
        }
}



void *value_container_get_data(value_container_t *vcont)
{
        return vcont->data;
}


void value_container_set_data(value_container_t *vcont, void *data)
{
        vcont->data = data;
}