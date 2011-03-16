# vim:ts=2:sw=2:et:

handle SIGPIPE nostop noprint
handle SIGUSR1 nostop noprint
handle SIGUSR2 nostop noprint
handle SIGPWR nostop noprint
handle SIGXCPU nostop noprint
handle SIGINFO nostop noprint
handle SIG32 nostop
# handle SIGSEGV nostop

set $LUA_TNONE = -1
set $LUA_TNIL = 0
set $LUA_TBOOLEAN = 1
set $LUA_TLIGHTUSERDATA = 2
set $LUA_TNUMBER = 3
set $LUA_TSTRING = 4
set $LUA_TTABLE = 5
set $LUA_TFUNCTION = 6
set $LUA_TUSERDATA = 7
set $LUA_TTHREAD = 8
set $LUA_TPROTO = 9
set $LUA_TUPVAL = 10
set $LUA_TDEADKEY = 11
set $LUA_TGLOBAL = 12

define dump_val
  set $v = $arg0
  if $v.tt == $LUA_TNONE
    printf "none\n"
  else
    if $v.tt == $LUA_TNIL
      printf "nil\n"
    else
      if $v.tt == $LUA_TBOOLEAN
        printf "boolean %s\n", $v.value.b ? "true" : "false"
      else
        if $v.tt == $LUA_TNUMBER
          printf "number %f\n", $v.value.n
        else
          if $v.tt == $LUA_TLIGHTUSERDATA
            printf "ludata %p\n", $v.value.p
          else
            dump_obj $v.value.gc
          end
        end
      end
    end
  end
end

document dump_val
  Shows a value
end

define dump_obj
  set $o = (GCheader*)$arg0
  if $o->tt == $LUA_TSTRING
    printf "string(%p,ref=%d) len=%d\n%s\n", $o, $o->ref, ((TString*)$o)->tsv.len, (char*)(((TString*)$o)+1)
  else
    if $o->tt == $LUA_TTABLE
      printf "table(%p,ref=%d)\n", $o, $o->ref
    else
      if $o->tt == $LUA_TUSERDATA
        printf "userdata(%p,ref=%d) metatable=%p\n", $o, $o->ref, ((Udata*)$o)->uv->metatable
      else
        if $o->tt == $LUA_TFUNCTION
          printf "closure(%p,ref=%d)\n", $o, $o->ref
          set $C = (Closure*)$o
          if $C->c.isC != 0
            printf "   [C:%p] ", $C->c.f
            if $C->c.fname != 0
              printf "%s\n   ", $C->c.fname
            end
            print/a $C->c.f
          else
            set $P = $C->l.p
            if $P->source != 0
              printf "   %s:%d\n", (char*)($P->source + 1), $P->linedefined
            else
              printf "   [VM]\n"
            end
          end
        else
          printf "object tt=%d %p,ref=%d\n", $o->tt, $o, $o->ref
        end
      end
    end
  end
end

document dump_obj
  Shows an object
end

define dump_tbl
  set $t = (Table*)$arg0
  printf "table %p ref=%d metatable=%p\n", $t, $t->gch.ref, $t->metatable
  set $i = 0
  while $i < $t->sizearray
    printf "arr[%d] = ", $i
    dump_val &$t->array[$i]
    set $i = $i + 1
  end
  set $i = 1 << $t->lsizenode
  while $i > 0
    set $i = $i - 1
    if $t->node[$i].i_val.tt != $LUA_TNIL
      printf "node[%d]\n  key = ", $i
      dump_val &$t->node[$i].i_key.tvk
      printf "  val = "
      dump_val &$t->node[$i].i_val
    end
  end
end

document dump_tbl
  Shows the contents of a table
end

define dump_bt
  set $ci = $arg0->ci
  printf "VM call stack in lua_State %p:\n\n", $arg0
  while $ci != 0 && $ci > $arg0->base_ci
    set $C = (Closure*)$ci->func->value.gc
    if $C->c.isC != 0
      printf "[C:%p] ", $C->c.f
      if $C->c.fname != 0
        printf "%s ", $C->c.fname
      end
      print/a $C->c.f
    else
      set $P = $C->l.p
      set $lpc = (int)($ci->savedpc - $P->code)
      if $P->source != 0
        printf "%s:%d @ pc=%d", (char*)($P->source + 1), $P->lineinfo[$lpc], $lpc
      else
        printf "[VM]"
      end
      printf "\n"
    end

    set $ci = $ci - 1
  end
  if $arg0->errorJmp != 0
    printf "\nError handler chain:\n"
    set $J = $arg0->errorJmp
    while $J != 0
      printf "  %s:%d\n", $J->file, $J->line
      set $J = $J->previous
    end
  else
    printf "\nNo error handler installed\n"
  end
end

document dump_bt
  Display a back trace; pass in a lua_State
end

define stacktop
  set $t = $arg0->top - 1
  set $i = -1
  while $t >= $arg0->base
    set $i = $t - $arg0->top
    printf "[%d] ", $i
    dump_val $t
    set $t = $t - 1
  end
end

document stacktop
  Show the top n from the stack; pass in a lua_State
end

set print symbol-filename on
set print pretty on

