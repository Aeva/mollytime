if host_machine.system() == 'windows'
  subdir('windows')
elif host_machine.system() == 'emscripten'
  cdata.set('SDL_SENSOR_EMSCRIPTEN', 1)
  subdir('emscripten')
else
  subdir('dummy')
endif

sources += files('SDL_sensor.c')
