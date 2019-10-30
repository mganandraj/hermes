from shutil import copyfile
from subprocess import call
import os
import errno

nuget_installed = 'D:\\ReactNative.Hermes.Windows_nuget\\ReactNative.Hermes.Windows\\installed\\'
# nuget_installed = 'D:\\tmp\\hermes-nuget\\'
hermes_build_dir_64 = 'D:\\github\\mgan_hermes\\build\\'
hermes_build_dir_32 = 'D:\\github\\mgan_hermes\\build_32\\'
hermes_source = 'D:\\github\\mgan_hermes\\hermes\\'

hermes_uwp_dir = 'C:\\Users\\anandrag.REDMOND\\source\\repos\\Hermes_uwp\\'

release_config = 'RelWithDebInfo'
debug_config = 'Debug'

def do_copy(src, dest):
    print('copying ' + src + ' to ' + dest)
    
    try:
        os.makedirs(os.path.dirname(dest))
    except OSError as exc: # Guard against race condition
        if exc.errno != errno.EEXIST:
            raise
    
    copyfile(src, dest)

def get_uwp_bin_source_path(triplet, bin_file_name, flavor):
    if triplet.startswith('x64'):
        return hermes_uwp_dir + 'x64\\' + flavor + '\\hermes_uwp\\' + bin_file_name
    else:
        return hermes_uwp_dir + flavor + '\\hermes_uwp\\' + bin_file_name


def copy_uwp(triplet, bin_file_name, nuget_path_seg):
    source_full_path = get_uwp_bin_source_path(triplet, bin_file_name, 'Release')
    dest_full_path = nuget_installed + triplet + '\\' + nuget_path_seg + '\\' + bin_file_name
    do_copy(source_full_path, dest_full_path)

    source_full_path = get_uwp_bin_source_path(triplet, bin_file_name, 'Debug')
    dest_full_path = nuget_installed + triplet + '\\debug\\' +  nuget_path_seg + '\\' + bin_file_name
    do_copy(source_full_path, dest_full_path)

def copy_hermes_build_bin(triplet, hermes_build_path, subpath, bin_file_name):

    if bin_file_name.endswith('.lib'):
        dest_path_segment = '\\lib\\'
    else:
        dest_path_segment = '\\bin\\'

    source_bin_file_name_aug = bin_file_name
    if bin_file_name.endswith('.exe'):
        source_bin_file_name_aug = 'bin\\' + source_bin_file_name_aug

    source_full_path = hermes_build_path + subpath  + release_config + '\\' + source_bin_file_name_aug
    dest_full_path = nuget_installed + triplet + dest_path_segment + bin_file_name
    do_copy(source_full_path, dest_full_path)

# Copy debug version of dll, lib & pdb, not exe
    if not bin_file_name.endswith('.exe'):
        source_full_path = hermes_build_path + subpath + debug_config + '\\' + bin_file_name
        dest_full_path = nuget_installed + triplet + '\\debug' + dest_path_segment + bin_file_name
        do_copy(source_full_path, dest_full_path)

def copy_headers(triplet):
    source_folder = hermes_source + 'API\\hermes\\'
    dest_folder = nuget_installed + triplet + '\\include\\hermes\\'
    filter = '*.h'
    
    call(['robocopy', source_folder, dest_folder, filter, '/S', '/LOG:robocopy_includes.txt'])

    source_folder = hermes_source + 'public\\hermes\\'
    call(['robocopy', source_folder, dest_folder, filter, '/S', '/LOG:robocopy_includes.txt'])

    source_folder = hermes_source + 'API\\jsi\\'
    dest_folder = nuget_installed + triplet + '\\include\\jsi_ref\\'
    call(['robocopy', source_folder, dest_folder, filter, '/S', '/LOG:robocopy_includes.txt'])


def do1():
    copy_hermes_build_bin('x64-windows', hermes_build_dir_64, '', 'hermes.exe')
    copy_hermes_build_bin('x64-windows', hermes_build_dir_64, 'API\\hermes\\', 'hermes.dll')
    copy_hermes_build_bin('x64-windows', hermes_build_dir_64, 'API\\hermes\\', 'hermes.pdb')
    copy_hermes_build_bin('x64-windows', hermes_build_dir_64, 'API\\hermes\\', 'hermes.lib')

    copy_hermes_build_bin('x86-windows', hermes_build_dir_32, 'API\\hermes\\', 'hermes.dll')
    copy_hermes_build_bin('x86-windows', hermes_build_dir_32, 'API\\hermes\\', 'hermes.pdb')
    copy_hermes_build_bin('x86-windows', hermes_build_dir_32, 'API\\hermes\\', 'hermes.lib')

    copy_uwp('x64-uwp', 'hermes.dll', 'bin')
    copy_uwp('x64-uwp', 'hermes.pdb', 'bin')
    copy_uwp('x64-uwp', 'hermes.lib', 'lib')
    
    copy_uwp('x86-uwp', 'hermes.dll', 'bin')
    copy_uwp('x86-uwp', 'hermes.pdb', 'bin')
    copy_uwp('x86-uwp', 'hermes.lib', 'lib')

    copy_headers('x64-windows')
    copy_headers('x86-windows')

    copy_headers('x64-uwp')
    copy_headers('x86-uwp')



def main():
    do1()

if __name__ == '__main__':
    main()