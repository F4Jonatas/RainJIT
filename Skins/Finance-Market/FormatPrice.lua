
-- FormatPrice.lua
-- adapted from http://lua-users.org/wiki/FormattingNumbers

-- add commas to separate thousands
function comma_value(amount)
  local formatted = amount
  while true do
    formatted, k = string.gsub(formatted, "^(-?%d+)(%d%d%d)", '%1,%2')
    if (k==0) then
      break
    end
  end
  return formatted
end

-- round number to nearest decimal places
function round(val, decPlaces)
  if (decPlaces) then
    return math.floor( (val * 10^decPlaces) + 0.5) / (10^decPlaces)
  else
    return math.floor(val+0.5)
  end
end

--  -----------------------------------------------------------
-- format_price formats output with comma to separate thousands
-- and rounded to given decimal places

function FormatPrice(amount, decPlaces )
  local str_amount,  formatted, famount, remain
  decPlaces = decPlaces or 2  -- default 2 decPlaces places

  famount = math.abs(round(amount,decPlaces))
  famount = math.floor(famount)
  remain = round(math.abs(amount) - famount, decPlaces)

-- add commas to separate the thousands
  formatted = comma_value(famount)

-- attach the decimal places portion
  if (decPlaces > 0) then
    remain = string.sub(tostring(remain),3)
    formatted = formatted .. "." .. remain ..
                string.rep("0", decPlaces - string.len(remain))
  end
  return formatted
end


