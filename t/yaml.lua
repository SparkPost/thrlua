-- vim:ts=2:sw=2:et:ft=lua:
require("Test.More");
require("yaml");

plan(11);

-- load
result = yaml.load("lua: rocks\npeppers: [ habanero, chipotle, jalapeno ]\n")

is(result.lua, 'rocks');
is(result.peppers[1], 'habanero');

-- anchors and aliases

yamlstr = [[
cars:
- &car1
  make: Audi
  model: S4
- &car2
  make: VW
  model: GTI
favorite: *car1
]]

result = yaml.load(yamlstr)

is(result.favorite.model, 'S4');
is(result.favorite, result.cars[1]);

-- metatable

is(getmetatable(result.cars)._yaml, 'sequence');
is(getmetatable(result.favorite)._yaml, 'map');

-- dump

is(yaml.dump({ VW = "GTI", Audi = "S4" }),[[
---
VW: GTI
Audi: S4
]]);

data = { "one", "two", nil, "three", dog = "cat" }
is(yaml.dump(data),[[
---
- one
- two
- ~
- three
]]);

data = { "one", "two", "three", dog = "cat" }
setmetatable(data, { _yaml = "map" })
is(yaml.dump(data),[[
---
1: one
2: two
3: three
dog: cat
]]);

colors = {
   reds = {
      normal = { hex = "FF0000", rgb = { 255, 0, 0 } },
      dark = { hex = "8B0000", rgb = { 139, 0, 0 } },
   }
}
colors.all = { colors.reds.normal, colors.reds.dark }

is(yaml.dump(colors),[[
---
all:
- &0
  hex: FF0000
  rgb:
  - 255
  - 0
  - 0
- &1
  hex: 8B0000
  rgb:
  - 139
  - 0
  - 0
reds:
  normal: *0
  dark: *1
]]);

is(yaml.dump({ VW = "GTI", Audi = "S4" }, { "one", "two", "three" }),[[
---
VW: GTI
Audi: S4
---
- one
- two
- three
]]);
