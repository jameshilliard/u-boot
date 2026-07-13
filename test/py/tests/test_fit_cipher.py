# SPDX-License-Identifier: GPL-2.0+
# Copyright 2026 James Hilliard

"""Check encrypted FIT loading and retries with enabled or ignored hashes."""

import hashlib
from pathlib import Path
import shutil

import pytest

import fit_util
import utils


# First two blocks of the public AES-256-CBC vector in NIST SP 800-38A.
KEY = bytes.fromhex('603deb1015ca71be2b73aef0857d7781'
                    '1f352c073b6108d72d9810a30914dff4')
IV = bytes(range(16))
PLAIN = bytes.fromhex('6bc1bee22e409f96e93d7e117393172a'
                      'ae2d8a571e03ac9c9eb76fac45af8e51')
CIPHER = bytes.fromhex('f58c4c04d6e5f1ba779eabfb5f7bfbd6'
                       '9cfc4e967edb808d679f777bc6702c7d')

FIT = '''/dts-v1/;
/ {
    description = "Encrypted FIT retry test";
    #address-cells = <1>;
    timestamp = <1>;
    images {
        kernel {
            description = "AES-256-CBC test vector";
            data = [%(cipher)s];
            data-size-unciphered = <32>;
            type = "kernel";
            arch = "sandbox";
            os = "linux";
            compression = "none";
            load = <0x4000000>;
            entry = <0x4000000>;
            cipher {
                algo = "aes256";
                key-name-hint = "retry";
                iv = [%(iv)s];
            };
            %(hashes)s
        };
    };
    configurations {
        default = "conf";
        conf {
            kernel = "kernel";
        };
    };
};
'''


@pytest.mark.boardspec('sandbox')
@pytest.mark.buildconfigspec('fit_cipher')
@pytest.mark.buildconfigspec('cmd_bootm')
@pytest.mark.buildconfigspec('cmd_fdt')
@pytest.mark.requiredtool('dtc')
@pytest.mark.requiredtool('fdtput')
@pytest.mark.parametrize('hash_mode', ('absent', 'ignored', 'enabled', 'mixed'))
@pytest.mark.parametrize('verify', ('yes', 'no'))
def test_fit_cipher_retry(ubman, hash_mode, verify):
    """Only a checked payload hash can guard destructive in-place decryption."""
    checked = hash_mode in ('enabled', 'mixed')
    hashes = ''
    if hash_mode in ('ignored', 'mixed'):
        hashes += 'hash-ignored { algo = "sha256"; uboot-ignore = <1>; };'
    if checked:
        digest = hashlib.sha256(CIPHER).digest().hex(' ')
        hashes += f'hash-checked {{ algo = "sha256"; value = [{digest}]; }};'

    name = f'fit-cipher-{hash_mode}-{verify}'
    fit = fit_util.make_dtb(ubman, FIT % {
        'cipher': CIPHER.hex(' '), 'iv': IV.hex(' '), 'hashes': hashes,
    }, name)
    dtb = fit_util.make_fname(ubman, f'{name}-control.dtb')
    payload = fit_util.make_fname(ubman, f'{name}-payload.bin')
    loaded = fit_util.make_fname(ubman, f'{name}-loaded.bin')
    old_dtb = ubman.config.dtb
    shutil.copyfile(old_dtb, dtb)
    utils.run_and_log(ubman, ['fdtput', '-p', '-t', 'bx', dtb,
                            '/cipher/key-aes256-retry', 'key',
                            *KEY.hex(' ').split()])

    try:
        ubman.config.dtb = dtb
        ubman.restart_uboot()
        ubman.run_command(f'setenv verify {verify}')
        ubman.run_command(f'host load hostfs - 1000000 {fit}')

        for attempt in range(2):
            output = ubman.run_command('bootm start 1000000')
            if attempt and checked:
                # A retry must reject the already consumed ciphertext.
                assert 'Bad hash value' in output
                assert 'Decrypting Data ... OK' not in output
            else:
                result = output.split('Decrypting Data ... ', 1)[1]
                assert result.splitlines()[0].endswith('OK')
                ubman.run_command('bootm loados')
                ubman.run_command(f'host save hostfs - 4000000 {loaded} 20')
                assert Path(loaded).read_bytes() == PLAIN

            ubman.run_command_list([
                'fdt addr 1000000',
                'fdt get addr fit_cipher_data /images/kernel data',
                f'host save hostfs - $fit_cipher_data {payload} 20',
            ])
            # Ignored/absent hashes must retain the allocating fallback.
            assert Path(payload).read_bytes() == (PLAIN if checked else CIPHER)
    finally:
        ubman.config.dtb = old_dtb
        ubman.restart_uboot()
