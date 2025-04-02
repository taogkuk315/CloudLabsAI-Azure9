import logging
import os
from datetime import timedelta

import pytest

from h2_certs import CertificateSpec, H2TestCA
from h2_env import H2TestEnv


class Dummy:
    pass


def pytest_report_header(config, startdir):
    env = H2TestEnv()
    return "mod_h2 [apache: {aversion}({prefix}), mpm: {mpm}]".format(
        prefix=env.prefix,
        aversion=env.get_httpd_version(),
        mpm=env.mpm_type
    )


@pytest.fixture(scope="session")
def env() -> H2TestEnv:
    level = logging.INFO
    console = logging.StreamHandler()
    console.setLevel(level)
    console.setFormatter(logging.Formatter('%(levelname)s: %(message)s'))
    logging.getLogger('').addHandler(console)
    logging.getLogger('').setLevel(level=level)
    env = H2TestEnv()
    cert_specs = [
        CertificateSpec(domains=env.domains, key_type='rsa4096'),
    ]
    ca = H2TestCA.create_root(name=env._http_tld,
                              store_dir=os.path.join(env.server_dir, 'ca'), key_type="rsa4096")
    ca.issue_certs(cert_specs)
    env.set_ca(ca)
    return env
