

class blank_tile:
    __next_id = 1

    # `inputs` is a mapping of unique names to default values which may be empty.
    # If a default value is set to None, then the tile either behaves as a
    # passthrough when not adequately connected, or it will implicitly rewrite
    # to a zero constant during codegen.
    inputs = dict()

    # `outputs` is a tuple of unique names which may be empty.
    outputs = tuple()

    def __init__(self, name):
        # `id` is a monotonically incleasing unique identifier used for
        # routing connections between nodes independent of board position.
        self.id = blank_tile.__next_id
        blank_tile.__next_id += 1

        # `name` is a human readable name that can be overriden by the player.
        self.name = name

    def __repr__(self):
        return f"<tile {self.id}: \'{self.name}\'>"

    def __str__(self):
        return self.name


class const_tile(blank_tile):
    outputs = ( "#" )

    def __init__(self, value=0, name="#"):
        super().__init__(name)
        self.value = value

    def __repr__(self):
        return f"<tile {self.id}: const {self.value}>"

    def __str__(self):
        return str(self.value)


class out_tile(blank_tile):
    inputs = { "out" : 0 }

    def __init__(self, name="out"):
        super().__init__(name)


class sin_tile(blank_tile):
    inputs = { "hz" : 440 }
    outputs = ( "amp", )

    def __init__(self, name="sin"):
        super().__init__(name)


class add_tile(blank_tile):
    inputs = { "+" : None }
    outputs = ( "=", )

    def __init__(self, name="add"):
        super().__init__(name)


class mul_tile(blank_tile):
    inputs = { "*" : None }
    outputs = ( "=", )
    def __init__(self, name="mul"):
        super().__init__(name)


class min_tile(blank_tile):
    inputs = { "min" : None }
    outputs = ( "=", )
    def __init__(self, name="min"):
        super().__init__(name)


class max_tile(blank_tile):
    inputs = { "max" : None }
    outputs = ( "=", )
    def __init__(self, name="max"):
        super().__init__(name)
