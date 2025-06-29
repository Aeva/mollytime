
import mollytime

for key, value in [(key, getattr(mollytime, key)) for key in dir(mollytime)]:
    try:
        if issubclass(value, mollytime.magic_tile):
            globals()[key] = value
    except:
        continue
del key
del value
