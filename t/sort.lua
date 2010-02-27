require("tap");
plan(3);

-- implementations of a sort function

-- extracted from Programming Pearls, page 110
function qsort(x,l,u,f)
 if l<u then
  local m=math.random(u-(l-1))+l-1	-- choose a random pivot in range l..u
  x[l],x[m]=x[m],x[l]			-- swap pivot to first position
  local t=x[l]				-- pivot value
  m=l
  local i=l+1
  while i<=u do
    -- invariant: x[l+1..m] < t <= x[m+1..i-1]
    if f(x[i],t) then
      m=m+1
      x[m],x[i]=x[i],x[m]		-- swap x[i] and x[m]
    end
    i=i+1
  end
  x[l],x[m]=x[m],x[l]			-- swap pivot to a valid place
  -- x[l+1..m-1] < x[m] <= x[m+1..u]
  qsort(x,l,m-1,f)
  qsort(x,m+1,u,f)
 end
end

function selectionsort(x,n,f)
 local i=1
 while i<=n do
  local m,j=i,i+1
  while j<=n do
   if f(x[j],x[m]) then m=j end
   j=j+1
  end
 x[i],x[m]=x[m],x[i]			-- swap x[i] and x[m]
 i=i+1
 end
end

function show(m,x)
 io.write(m,"\n\t")
 local i=1
 while x[i] do
  io.write(x[i])
  i=i+1
  if x[i] then io.write(",") end
 end
 io.write("\n")
end

function cmp_table(A, B)
  local i = 1
  if table.maxn(A) ~= table.maxn(B) then
    return 0
  end
  while A[i] do
    if A[i] ~= B[i] then
      return 0
    end
    i = i + 1
  end
  return 1
end

-- array to be sorted
x={"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"}
sorted={"Apr","Aug","Dec","Feb","Jan","Jul","Jun","Mar","May","Nov","Oct","Sep"}
revs={"Sep","Oct","Nov","May","Mar","Jun","Jul","Jan","Feb","Dec","Aug","Apr"}

local n=table.maxn(x)
qsort(x,1,n,function (x,y) return x<y end)
ok(cmp_table(x, sorted), "qsort sorted things correctly")

selectionsort(x,n,function (x,y) return x>y end)
ok(cmp_table(x, revs), "reverse selection sort")

qsort(x,1,n,function (x,y) return x<y end)
ok(cmp_table(x, sorted), "qsort sorted things correctly")


