#------------------------------------------------------------------------------
# Script to build the Hone (Host-Network) Packet-Process Correlator
# for Windows
#
# Copyright (c) 2014-2015 Battelle Memorial Institute
# Licensed under a modification of the 3-clause BSD license
# See License.txt for the full text of the license and additional disclaimers
#
# Authors
#   Alexis J. Malozemoff <alexis.malozemoff@pnnl.gov>
#   Peter L. Nordquist <peter.nordquist@pnnl.gov>
#   Richard L. Griswold <richard.griswold@pnnl.gov>
#   Ruslan A. Doroshchuk <ruslan.doroshchuk@pnnl.gov>
#------------------------------------------------------------------------------

import argparse
import datetime
import glob
import os
import re
import shutil
import subprocess
import sys
import traceback

#------------------------------------------------------------------------------
def build_installer(args):
	if not os.path.exists(args.build_root):
		print 'Error: Build root {0} does not exist\n' \
				'Check version or try rebuilding'.format(args.build_root)
		exit(1)

	cwd = os.getcwd()
	files_dir = '{0}/files'.format(args.build_root)
	archs = [['32-bit', 'x86'], ['64-bit', 'x64']]
	installer_files = []

	print ' * Building Hone installer {0}'.format(args.version)

	# Create destination directories
	print ' * Copying files...'
	dst_dir = '{0}/temp/installer'.format(args.build_root)
	if os.path.exists(dst_dir):
		clean_dir(dst_dir)
	else:
		os.makedirs(dst_dir)

	# Copy driver files
	for arch in archs:
		drv_dir = '{0}/{1}'.format(dst_dir, arch[1])
		os.makedirs(drv_dir)
		copy_sys('{0}/build_files/{1}'.format(args.build_root, arch[1]), drv_dir)

	# Copy remaining files needed by installer
	copy_exe('{0}/build_files/x86'.format(args.build_root), dst_dir)
	copy_all('{0}/installer'.format(args.script_dir), dst_dir)
	copy_file('{0}/License.txt'.format(args.script_dir), dst_dir)
	copy_file('{0}/Readme.html'.format(args.script_dir), dst_dir)
	if not args.no_sign:
		cert_file = os.path.basename(args.cert_file)
		copy_file(args.cert_file, dst_dir)

	# Create installer version file
	print ' * Creating version file...'
	with open('{0}/version.ini'.format(dst_dir), 'w') as f:
		f.write('#define MyAppVersion "{0}"\n'.format(args.version))

	os.chdir(dst_dir)

	# Sign driver files
	if not args.no_sign:
		for arch in archs:
			os.chdir(arch[1])
			sign_file(args.signtool, 'hone.sys', '../{0}'.format(cert_file), args.cert_pass)
			timestamp_file(args.signtool, 'hone.sys')
			os.chdir('..')

	# Build the installer
	try:
		cmdline = [args.iscc, 'hone.iss']
		retcode = subprocess.call(cmdline)
	except Exception:
		traceback.print_exc(1)
		exit(1)
	if retcode != 0:
		print 'Error: Inno Setup compilation failed with error code {0}'.format(retcode)
		exit(1)

	# Copy the installer to the output directory
	installer_dir = '{0}/installers'.format(args.build_root)
	if not os.path.exists(installer_dir):
		os.makedirs(installer_dir)

	installer_src = '{0}/Output/Hone.exe'.format(dst_dir)
	installer_dst = '{0}/Hone-{1}-win7.exe'.format(installer_dir, args.version)
	shutil.copy(installer_src, installer_dst)

	# Sign the installer
	if not args.no_sign:
		sign_file(args.signtool, installer_dst, cert_file, args.cert_pass)
		timestamp_file(args.signtool, installer_dst)

	installer_files += [installer_dst,]
	os.chdir(cwd)
	return installer_files

#------------------------------------------------------------------------------
def build_sensor(args):
	if args.debug:
		build_type = 'chk'
	else:
		build_type = 'fre'
	build_helper = '{0}/build_helper.bat'.format(args.script_dir)

	archs = [['32-bit', 'x86', 'x86', 'i386'], ['64-bit', 'x64', 'amd64', 'amd64']]
	for arch in archs:
		print ' * Building {0} Hone version {1}'.format(arch[0],
				args.version)

		cmd = [build_helper, args.script_dir, build_type, arch[1]]
		try:
			proc = subprocess.call(cmd)
		except:
			traceback.print_exc(1)
			exit(1)

		for proj in ('hone', 'honeutil'):
			build_dir = '{0}/{1}/obj{2}_win7_{3}/{4}'.format(args.script_dir, proj,
					build_type, arch[2], arch[3])

			dst_dir = '{0}/build_files/{1}'.format(args.build_root, arch[1])
			if not os.path.exists(dst_dir):
				os.makedirs(dst_dir)
			copy_exe(build_dir, dst_dir)
			copy_sys(build_dir, dst_dir)

			dst_dir = '{0}/debug_symbols/{1}'.format(args.build_root, arch[1])
			if not os.path.exists(dst_dir):
				os.makedirs(dst_dir)
			copy_pdb(build_dir, dst_dir)

#------------------------------------------------------------------------------
def clean_dir(path):
	for root, dirs, files in os.walk(path):
		for f in files:
			file = '{0}/{1}'.format(root, f)
			# print '   Removing {0}'.format(file)
			os.unlink(file)
		for d in dirs:
			dir = '{0}/{1}'.format(root, d)
			# print '   Removing {0}'.format(dir)
			shutil.rmtree(dir)

#------------------------------------------------------------------------------
def copy_all(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*'.format(src_dir)):
		fname = fname.replace('\\', '/')
		if os.path.isfile(fname):
			print '   {0}'.format(fname)
			shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_exe(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*.exe'.format(src_dir)):
		fname = fname.replace('\\', '/')
		print '   {0}'.format(fname)
		shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_file(src_file, dst_dir):
	fname = src_file.replace('\\', '/')
	print '   {0}'.format(fname)
	shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_pdb(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*.pdb'.format(src_dir)):
		if fname.find('vc90.pdb', -8) == -1:
			fname = fname.replace('\\', '/')
			print '   {0}'.format(fname)
			shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def copy_sys(src_dir, dst_dir):
	for fname in glob.iglob('{0}/*.sys'.format(src_dir)):
		fname = fname.replace('\\', '/')
		print '   {0}'.format(fname)
		shutil.copy(fname, dst_dir)

#------------------------------------------------------------------------------
def create_version_file(path, version):
	'''Create version file'''
	time = datetime.datetime.now()
	year = time.year % 100;
	timestamp = '{0}-{1:02}-{2:02} {3:02}:{4:02}:{5:02}'.format(time.year,
			time.month, time.day, time.hour, time.minute, time.second)

	version_list = ','.join(map(lambda i: str(int(i)), version.split('.')))

	with open('{0}/version.h'.format(path), 'w') as f:
		f.write('''\
// -=-=-=-=-=- DO NOT EDIT THIS FILE! -=-=-=-=-=-
// Hone (Host-Network) Packet-Process Correlator version number
// Automatically generated by build.py on {0}
#define HONE_PRODUCTVERSION      {1}
#define HONE_PRODUCTVERSION_STR  "{2}"
'''.format(timestamp, version_list, version))

#------------------------------------------------------------------------------
def parse_args(argv):
	help_epilog='''\
The script builds the Hone (Host-Network) Packet-Process Correlator
for Windows.  It creates the build directories as follows:
+--------------------------+-----------------------------------------+
| hone_n.n.n_debug|release | Build root                              |
|   build_files            |   Files created by the build process    |
|   debug_symbols          |   PDB files for drivers and executables |
|   installers             |   Installers                            |
|   temp                   |   Temporary files                       |
|     installer            |     Temporary installer files           |
+--------------------------+-----------------------------------------+
You can delete the temp directory after the script finishes.
'''

	parser = argparse.ArgumentParser(add_help=False, epilog=help_epilog,
			formatter_class=argparse.RawTextHelpFormatter)

	group = parser.add_argument_group()
	group.add_argument('-h', '--help', action='help',
			help='show this help message and exit')
	group.add_argument('-s', dest='stage', default='a',
			help='stage to run (a=all [default], b=build, i=create installer)')
	group.add_argument('-o', dest='output', metavar='DIR',
			help='output directory (default: script directory)')
	group.add_argument('-v', dest='version',
			help='set build version as "major.minor" or "major.minor.build"')
	group.add_argument('-d', action='store_true', dest='debug', default=False,
			help='build debug versions of programs (default: release)')
	group.add_argument('-n', action='store_true', dest='no_sign', default=False,
			help='disable signing of the driver and installer')
	group.add_argument('-c', dest='cert_file', metavar='FILE',
			help='path to certificate file')
	group.add_argument('-p', dest='cert_pass', metavar='PASS', default='changeit',
			help='password for certificate file (default: changeit)')

	args = parser.parse_args(argv[1:])
	error = []

	# Validate arguments
	if not re.match('[abi]*$', args.stage):
		error.append('Invalid stage specification "{0}"'.format(args.stage))
	if 'a' in args.stage:
		args.stage = ['b', 'i']
	else:
		args.stage = sorted(set(list(args.stage)))

	if 'i' in args.stage:
		for pf_env in ['ProgramFiles', 'ProgramFiles(x86)']:
			pf = os.environ.get(pf_env)
			if pf:
				iscc = '{0}/Inno Setup 5/iscc.exe'.format(pf)
				if os.path.exists(iscc):
					args.iscc = iscc
					break
		if not hasattr(args, 'iscc'):
			error.append('Inno Setup 5 compiler not found.  Ensure Inno Setup 5 '
					'is installed to the standard Program Files location.')

		if not args.no_sign:
			wdk_dir = os.environ.get('WDKPATH')
			if wdk_dir is None:
				error.append('WDKPATH environment variable is undefined')
			else:
				args.signtool = '{0}/bin/x86/signtool'.format(wdk_dir.replace('\\', '/'))
			if not args.cert_file:
				error.append('Certificate file is required when building installer '
						'unless signing is disabled')

	if not args.version:
		error.append('Version required')
	elif not re.match('[0-9]+\.[0-9]+(\.[0-9]+)?$', args.version):
		error.append('Invalid version "{0}": version format must be "major.minor" '
				'or "major.minor.build", where each version element is a number'
				.format(args.version))
	else:
		version = args.version.split('.')
		if int(version[0]) > 255:
			error.append('Invalid version "{0}": major version number '
					'must be less than 256'.format(args.version))
		if int(version[1]) > 255:
			error.append('Invalid version "{0}": minor version number '
					'must be less than 256'.format(args.version))
		if (len(version) > 2) and (int(version[2]) > 65535):
			error.append('Invalid version "{0}": build version number '
					'must be less than 65536'.format(args.version))

	if error:
		error.sort()
		parser.error('\n * {0}'.format('\n * '.join(error)))

	# Set script path, build root, and output
	args.script_dir = os.path.abspath(os.path.dirname(argv[0])).replace('\\', '/')

	if args.output:
		args.output = os.path.abspath(args.output).replace('\\', '/')
	else:
		args.output = args.script_dir

	if not args.debug:
		build_type = 'release'
	else:
		build_type = 'debug'
	args.build_root = '{0}/hone_{1}_{2}'.format(args.output, args.version, build_type)

	return args

#------------------------------------------------------------------------------
def sign_file(signtool, filename, cert_file, cert_pass):
	print ' * Signing {0}...'.format(filename)
	cmdline = [signtool, 'sign', '/v', '/f', cert_file, '/p',
			cert_pass, filename]
	try:
		retcode = subprocess.call(cmdline)
	except Exception:
		traceback.print_exc(1)
		exit(1)
	if retcode != 0:
		print 'Error: Signing failed with error code {0}'.format(retcode)
		exit(1)

#------------------------------------------------------------------------------
def timestamp_file(signtool, filename):
	# From http://stackoverflow.com/questions/2872105/alternative-timestamping-services-for-authenticode
	servers = [
		'http://timestamp.comodoca.com/authenticode',
		'http://timestamp.digicert.com',
		'http://timestamp.entrust.net/TSS/AuthenticodeTS',
		'http://timestamp.globalsign.com/scripts/timestamp.dll',
		'http://timestamp.verisign.com/scripts/timstamp.dll',
		'http://tsa.starfieldtech.com',
	]

	for server in servers:
		print ' * Timestamping {0} using {1}...'.format(filename, server)
		cmdline = [signtool, 'timestamp', '/t', server, filename]
		try:
			retcode = subprocess.call(cmdline)
		except Exception:
			traceback.print_exc(1)
			exit(1)
		if retcode == 0:
			return # Return if successful, try next server if failed

	print 'Error: Timestamping failed with all available servers'
	exit(1)

#------------------------------------------------------------------------------
def main(argv):
	args = parse_args(argv)

	if 'b' in args.stage:
		if not os.path.exists(args.build_root):
			os.makedirs(args.build_root)
		create_version_file(args.script_dir, args.version)
		build_sensor(args)

	installer_files = []
	if 'i' in args.stage:
		installer_files = build_installer(args)

	print ' * Build completed successfully!'
	if installer_files:
		for installer_file in installer_files:
			print '   {0}'.format(installer_file)

#------------------------------------------------------------------------------
if __name__ == '__main__':
	try:
		main(sys.argv)
	except KeyboardInterrupt:
		pass
