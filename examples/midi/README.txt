These patches require a MIDI stream to function.

On Windows, MIDI functionality is provided via mmeapi (aka the "Windows Multimedia API").
Simply plug in a MIDI control to your computer before starting Mollytime, and you will be
able to play these patches with it.

On Linux, MIDI functionality is provided via ALSA, which will likely require you to set
up connections manually with a separate tool.  This is usually accomplished with the
`aconnect` command if you prefer the commandline, otherwise graphical tools like Helvum may
also be used.  A positive consequence of this indirection is it is easier to use Mollytime
with sequencer software.
