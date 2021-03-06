#!/usr/bin/env python

"""
Test variable expansion of '<!()' syntax commands.
"""

import os

import TestGyp

test = TestGyp.TestGyp(format='gypd')

expect = test.read('commands.gyp.stdout')

# Set $HOME so that gyp doesn't read the user's actual
# ~/.gyp/include.gypi file, which may contain variables
# and other settings that would change the output.
os.environ['HOME'] = test.workpath()

test.run_gyp('commands.gyp',
             '--debug', 'variables', '--debug', 'general',
             stdout=expect)

# Verify the commands.gypd against the checked-in expected contents.
#
# Normally, we should canonicalize line endings in the expected
# contents file setting the Subversion svn:eol-style to native,
# but that would still fail if multiple systems are sharing a single
# workspace on a network-mounted file system.  Consequently, we
# massage the Windows line endings ('\r\n') in the output to the
# checked-in UNIX endings ('\n').

contents = test.read('commands.gypd').replace('\r\n', '\n')
expect = test.read('commands.gypd.golden')
if not test.match(contents, expect):
  print "Unexpected contents of `commands.gypd'"
  self.diff(expect, contents, 'commands.gypd ')
  test.fail_test()

test.pass_test()
