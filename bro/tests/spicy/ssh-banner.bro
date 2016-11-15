#
# @TEST-EXEC: bro -r ${TRACES}/ssh-single-conn.trace ssh.evt %INPUT >output
# @TEST-EXEC: btest-diff output
#

event ssh::banner(c: connection, is_orig: bool, version: string, software: string)
	{
	print "SSH banner", c$id, is_orig, version, software, Hilti::is_compiled();
	}
