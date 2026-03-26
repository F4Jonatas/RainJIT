
# RainJIT - Lua Binding para Rainmeter

## 📋 Visão Geral
Controle programático de meters Rainmeter.

<br>
<br>


## Criação de Instância
```lua
local meter = require('meter')
local m = meter('MyMeter')
```

<br>
<br>


## Visibilidade
```lua
-- Métodos básicos
m:show()     -- Mostra o meter
m:hide()     -- Esconde o meter
m:toggle()   -- Alterna visibilidade
m:update()   -- Atualiza o meter
```

<br>
<br>


## Posicionamento
```lua
-- Movimento absoluto
meter:move(150, 75)

-- Getter/Setter individual
local x = meter:left()     -- Obtém posição X
meter:left( 100 )          -- Define posição X

local y = meter:top()      -- Obtém posição Y
meter:top( 50 ):update()   -- Define posição Y e atualiza

-- Em Lua, converte string para número quando necessário
local numX = tonumber(meter:left()) or 0
```

<br>
<br>


## Dimensões
```lua
-- Largura
local width = meter:width()      -- Obtém largura
meter:width( 300 ):update()      -- Define largura

-- Altura
local height = meter:height()    -- Obtém altura
meter:height( 200 )              -- Define altura

-- Method chaining completo
meter:move( 100, 50 ):width( 300 ):height( 200 ):update()
```

<br>
<br>


## Opções Genéricas
```lua
-- Getter: retorna valor da opção
local text = meter:option( 'Text' )
local font = meter:option( 'FontFace' )

-- Setter: retorna self para chaining
meter:option( 'Text', 'Hello World' )
  :option( 'FontSize', 12 )
  :option( 'FontColor', '255,255,255' )
  :update()

-- Opções especiais
meter:option( 'SolidColor', '0,0,0,150' )
  :option( 'AntiAlias', 1 )
```

<br>
<br>


## Propriedades do Meter
```lua
-- Nome do meter (apenas leitura)
print(meter.name)

-- Tipo do meter (apenas leitura)
print(meter.type)  -- String, Image, etc.
```
