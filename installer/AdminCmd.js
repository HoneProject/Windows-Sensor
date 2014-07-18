// Run an elevated command prompt that starts in the same folder as the script
//
// Copyright (c) 2014 Battelle Memorial Institute
// Licensed under a modification of the 3-clause BSD license
// See License.txt for the full text of the license and additional disclaimers

// Concatenate script arguments to create the title for the shell window
var title = '';
var sep = '';
objArgs = WScript.Arguments;
for (i = 0; i < objArgs.length; i++) {
	title = title + sep + objArgs(i)
	sep = ' '
}

// Get the path to the script
var fso = new ActiveXObject('Scripting.FileSystemObject')
var path = fso.GetParentFolderName(WScript.ScriptFullName)

// Create the command line
var cmd = '/k cd "' + path + '"'
if (title.length > 0) {
	cmd = cmd + ' & title ' + title
}

// Start the shell
var app = new ActiveXObject('Shell.Application')
app.ShellExecute('cmd.exe', cmd, '', 'runas')
