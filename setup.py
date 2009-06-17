#!/usr/bin/env python

from ez_setup import use_setuptools
use_setuptools()

import os
from setuptools import setup, find_packages
from setuptools.command.install import install

PRELUDE_CORRELATOR_VERSION = "0.1"
LIBPRELUDE_REQUIRED_VERSION = "0.9.23"

class my_install(install):
        def run(self):
                if self.prefix:
                        self.conf_prefix = self.prefix + "/etc/prelude-correlator"
                else:
                        self.conf_prefix = "/etc/prelude-correlator"

                if not os.path.exists(self.prefix + "/var/lib/prelude-correlator"):
                        os.makedirs(self.prefix + "/var/lib/prelude-correlator")

                self.init_siteconfig()
                install.run(self)

        def init_siteconfig(self):
                config = open("PreludeCorrelator/siteconfig.py", "w")
                print >> config, "conf_dir = '%s'" % os.path.abspath(self.conf_prefix)
                print >> config, "lib_dir = '%s'" % os.path.abspath(self.prefix + "/var/lib/prelude-correlator")
                print >> config, "libprelude_required_version = '%s'" % LIBPRELUDE_REQUIRED_VERSION
                config.close()


setup(
        name="prelude-correlator",
        version=PRELUDE_CORRELATOR_VERSION,
        maintainer = "Yoann Vandoorselaere",
        maintainer_email = "yoann.v@prelude-ids.com",
        author = "Yoann Vandoorselaere",
        author_email = "yoann.v@prelude-ids.com",
        url = "http://www.prelude-ids.com",
        download_url = "http://www.prelude-ids.com/development/download/",
        description = "Prelude-Correlator perform real time correlation of events received by Prelude",
        long_description = """
Prelude-Correlator perform real time correlation of events received by Prelude.

Several isolated alerts, generated from different sensors, can thus
trigger a single CorrelationAlert should the events be related. This
CorrelationAlert then appears within the Prewikka interface and
indicates the potential target information via the set of correlation
rules.

Signature creation with Prelude-Correlator is based on the Python
programming language. Prelude's integrated correlation engine is
distributed with a default set of correlation rules, yet you still
have the opportunity to modify and create any correlation rule that
suits your needs.
""",
        classifiers = [ "Development Status :: 4 - Beta",
                        "Environment :: Console",
                        "Intended Audience :: System Administrators",
                        "License :: OSI Approved :: GNU General Public License (GPL)",
                        "Natural Language :: English",
                        "Operating System :: OS Independent",
                        "Programming Language :: Python",
                        "Topic :: Security",
                        "Topic :: System :: Monitoring" ],

        packages = find_packages(),
        entry_points = {
                'console_scripts': [
                        'prelude-correlator = PreludeCorrelator.main:main',
                ],

                'PreludeCorrelator.plugins': [
                        'BruteForcePlugin = PreludeCorrelator.plugins.bruteforce:BruteForcePlugin',
                        'BusinessHourPlugin = PreludeCorrelator.plugins.businesshour:BusinessHourPlugin',
                        'DshieldPlugin = PreludeCorrelator.plugins.dshield:DshieldPlugin',
                        'FirewallPlugin = PreludeCorrelator.plugins.firewall:FirewallPlugin',
                        'OpenSSHAuthPlugin = PreludeCorrelator.plugins.opensshauth:OpenSSHAuthPlugin',
                        'EventScanPlugin = PreludeCorrelator.plugins.scan:EventScanPlugin',
                        'EventStormPlugin = PreludeCorrelator.plugins.scan:EventStormPlugin',
                        'EventSweepPlugin = PreludeCorrelator.plugins.scan:EventSweepPlugin',
                        'WormPlugin = PreludeCorrelator.plugins.worm:WormPlugin'
                ]
        },

        cmdclass = { 'install': my_install }
)