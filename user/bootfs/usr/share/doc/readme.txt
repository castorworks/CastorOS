CastorOS keeps its files in one tree:

  /bin            programs: type a name to run one, ls /bin to see them
  /etc/rc         commands the shell runs at boot
  /usr/share/doc  these notes
  /home           a place for your own files
  /tmp            scratch space, kept in memory and empty again after a restart

When the system was started from its own disk, everything outside /tmp is on
that disk and stays. Started any other way (from a CD, or with no disk) the
whole tree is in memory, a fresh copy of what the system was built with.
See paths.txt for how files are named.
