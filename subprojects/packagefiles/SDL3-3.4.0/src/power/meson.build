if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'darwin'
  subdir('macos')
elif host_machine.system() == 'emscripten'
  cdata.set('SDL_POWER_EMSCRIPTEN', 1)
  subdir('emscripten')
else
  subdir('linux')
endif

sources += files('SDL_power.c')
