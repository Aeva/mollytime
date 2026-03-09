sources += files('SDL_locale.c')

if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'linux' or host_machine.system() == 'darwin'
  subdir('unix')
elif host_machine.system() == 'emscripten'
  subdir('emscripten')
else
  error('Platform not supported yet.')
endif
