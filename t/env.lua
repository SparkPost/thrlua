require("Test.More");
plan(1);

-- read environment variables as if they were global variables
setmetatable(
  getfenv(), {
    __index = function (t, i)
      return os.getenv(i)
    end
  }
)

-- the env var is set by runtests
is(THIS_ENV_VAR_IS_SET_FOR_THE_ENV_TEST, "w00t", "found env var");
