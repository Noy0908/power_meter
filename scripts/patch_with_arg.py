import os
import sys
from shutil import copyfile
from sys import exit
from pathlib import Path
import detools
import argparse

FILE_PATH = Path(__file__).resolve().parent
paths = (FILE_PATH / "../binaries/signed_images").resolve()
sign_patch = (FILE_PATH / "../binaries/patches/signed_patch.bin").resolve()
patch_path = str(FILE_PATH / "../binaries/patches/patch")

source_path = ''
command = "detools create_patch --compression heatshrink "
source_version = ''
target_version = ''
image_version=''
target_path = ''
ZEPHYR_BASE = os.environ['ZEPHYR_BASE']


def padded_hex(s, p):
    return '0x' + s[2:].zfill(p)


def create_patch_file(path):
    global command
    global patch_path
    global source_path
    global source_version
    global target_version
    source_count = 0
    target_count = 0.
    target_patch = ''

    try:
        files = os.listdir(path)
        for file_name in files:
            # print("file_name="+file_name)
            if "source_" in file_name:
                source_count += 1
            elif "target_" in file_name:
                target_count += 1
    except:
        print("file path : [\"%s\"] error!\r\n" % (path))
        input("Press any key to continue . . .")
    else:
        if (source_count != 1):
            print("Multiple source files:%d!!!\r\n" % source_count)
            input("Press any key to continue . . .")
        if (target_count != 1):
            print("Multiple target files:%d!!!\r\n" % target_count)
            input("Press any key to continue . . .")

        for file_name in files:
            if "source_" in file_name:
                source_path = os.path.normpath(os.path.join(path, file_name))
                source_version = file_name.split("_")[1].split(".bin")[0]
                # print("source file:%s  source_version:%s  source_path:%s"  %(file_name,source_version,source_path))
            elif "target_" in file_name:
                target_path = os.path.normpath(os.path.join(path, file_name))
                target_version = file_name.split("_")[1].split(".bin")[0]
                # print("target file:%s  target_version:%s  target_path:%s"  %(file_name,target_version,target_path))

        patch_path += ("_from_" + source_version + "_to_" + target_version + ".bin")

        ffrom = open(source_path, 'rb')
        fto = open(target_path, 'rb')
        fpatch = open(patch_path, 'wb+')

        detools.create_patch(ffrom, fto, fpatch, compression='heatshrink')
        ffrom.close()
        fto.close()
        fpatch.close()


def inject_hash_to_patch_file(sign_patch):
    # print("source_path:%s" %source_path)
    size = os.stat(patch_path).st_size
    f = open(patch_path, 'r+b')
    contents = f.read()
    f.seek(0)
    f.write('NEWP'.encode())
    f.write(size.to_bytes(4, byteorder='little'))
    # write source hash value to patch file
    with open(source_path, "rb") as file_obj:
        file_obj.seek(12)
        value = file_obj.read(4)
        offset = int.from_bytes(value, byteorder='little')
        print("file_size = 0X%08x\n" % (offset))
        file_obj.seek(offset + 0x800 + 0x08)
        source_hash = file_obj.read(0x20)
        print(source_hash)
        f.write(source_hash)
    # write file contents to new file
    f.write(contents)
    f.close()

def sign_patch_file(sign_path):
    inject_hash_to_patch_file(sign_patch)
    # excute signature command
    try:
        with open(sign_path, encoding='utf-8') as file_obj:
            text = eval(file_obj.read())
            sign_command = text.strip(
                '\r\n') + ' ' + patch_path + ' ' + patch_path.replace("patch_from", "signed_patch_from")
            print(sign_command)
            os.system(sign_command)
    except:
        print(
            "Please check your imgtool command: [\"%s\"]!!!\r\n" % sign_command)
        input("Press any key to continue . . .")
    else:
        # copy and rename the signed patch file , which will be used by make flash-patch command
        copyfile(patch_path.replace("patch_from",
                 "signed_patch_from"), sign_patch)

    print("\nFile copy done!\n")


def awklike(field_str, filename):
    """ A function like unix awk to split string"""
    ret = ''
    try:
        with open(filename, encoding='utf8') as file_pointer:
            for line in file_pointer:
                if field_str in line:
                    ret = line.replace(field_str, '').replace(
                        '\n', '').replace('"', '')
                    break
    except (OSError, IOError) as exp:
        print(exp)
    return ret


def sign_patch_file_by_config(build_dir):
    global patch_path
    global image_version

    inject_hash_to_patch_file(sign_patch)

    if os.name == 'nt':
        folder_slash = '\\'
    else:
        folder_slash = '/'

    mcuboot_key = awklike('CONFIG_BOOT_SIGNATURE_KEY_FILE=', build_dir +
                              '/mcuboot/zephyr/.config')

    # '\\' is used for Windows, '/' is used for other Operating Systems like Linux.
    if ('/' in mcuboot_key) or ('\\' in mcuboot_key):
        print('absolute path')
        # Zephyr script convert folder separator to '/'. Should do the same here no matter Windows or not.
        if folder_slash in mcuboot_key:
            mcuboot_key.replace(folder_slash, '/')
    else:
        print('relative path')
        mcuboot_key = ZEPHYR_BASE + '/../bootloader/mcuboot/' + mcuboot_key

    image_version = awklike('CONFIG_MCUBOOT_IMGTOOL_SIGN_VERSION=',
                            build_dir + '/zephyr/.config')
    fw_info_offset = awklike('CONFIG_PM_PARTITION_SIZE_MCUBOOT_PAD=', build_dir +
                             '/mcuboot/zephyr/.config')
    pm_mcuboot_primary_size = int(awklike('PM_MCUBOOT_PRIMARY_APP_SIZE=', build_dir +
                                            '/pm.config'), 16)

    # patch_path = patch_path.replace("patch_from", "signed_patch_from")
    # print ('patch_path: ' + patch_path)
    # Generate net_core_app_update.bin
    os_cmd = f'{sys.executable} {ZEPHYR_BASE}/../bootloader/mcuboot/scripts/imgtool.py sign --key\
    {mcuboot_key} --header-size {fw_info_offset} --align 4 --version {image_version}\
    --pad-header --slot-size {pm_mcuboot_primary_size}  {patch_path}  ' + patch_path.replace("patch_from", "signed_patch_from")
    ret_val = os.system(os_cmd)
    if ret_val:
        raise Exception('python error: ' + str(ret_val))

def generate_dfu_zip(build_dir):

    global patch_path
    global image_version

    copyfile(patch_path.replace("patch_from", "signed_patch_from"), "app_update.bin")

    board = awklike('CONFIG_BOARD=', build_dir + '/zephyr/.config')
    soc = awklike('CONFIG_SOC=', build_dir + '/zephyr/.config')
    loadaddr = awklike('PM_APP_ADDRESS=', build_dir + '/pm.config')
# /generate_zip.py --bin-files
# C:/ncs/v2.6.99-cs1_gazell/nrf/applications/nrf_desktop/build_54l/zephyr/app_update.bin
# --output C:/ncs/v2.6.99-cs1_gazell/nrf/applications/nrf_desktop/build_54l/zephyr/dfu_application.zip
# --name nrf_desktop
# --meta-info-file C:/ncs/v2.6.99-cs1_gazell/nrf/applications/nrf_desktop/build_54l/zephyr/zephyr.meta
# load_address=0xc800
# version_MCUBOOT=2.0.0+0
# type=application
# board=nrf54l15pdk_nrf54l15_cpuapp
# soc=nrf54l15_cpuapp"
    os_cmd = f'{sys.executable} {ZEPHYR_BASE}/../nrf/scripts/bootloader/generate_zip.py\
    --bin-files "app_update.bin"\
    --output dfu_application.zip\
    --name nrf_appl\
    --meta-info-file {build_dir}/zephyr/zephyr.meta\
    load_address={loadaddr}\
    version_MCUBOOT={image_version}\
    type=application\
    board={board}\
    soc={soc}'

    ret_val = os.system(os_cmd)
    if ret_val:
        raise Exception('python error: ' + str(ret_val))
    copyfile("dfu_application.zip", build_dir + "/dfu_application.zip")
    copyfile("dfu_application.zip", Path(patch_path).resolve().parent / "dfu_application.zip")
    os.remove("app_update.bin")

if __name__ == "__main__":

    parser = argparse.ArgumentParser(
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description=(
            "This script create and sign delta dfu patch  "
        ),
        allow_abbrev=False
    )
    parser.add_argument(
        "-d",
        "--build-dir",
        type=str,
        help="Assign original build folder to retrieve configs",
    )
    options = parser.parse_args(args=sys.argv[1:])

    create_patch_file(paths)
    if (options.build_dir == None):
        sign_patch_file(FILE_PATH/"signature.txt")
    else:
        sign_patch_file_by_config(options.build_dir)

    generate_dfu_zip(options.build_dir)
    input("Press any key to continue . . .")
