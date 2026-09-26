
[Variables]
NumberToDisplay=10
; Do not change NumberToDisplay without altering the .ini file as well
DoOddEven=1

; Change these to set text colors and indicator "dead / off" color.
EvenColor=255,255,255,255
OddColor=255,255,255,255
DeadColor=150,150,150,255

; Change this to set the amount of information in the tooltip.
; 0=No Tooltip
; 1=Author and post subject
; 2=Author and post age
; 3=Author, post subject and post age
TooltipOption=3

; You can change NewMinutes to any number of minutes to indicate "new".
NewMinutes=30
NewColor=207,250,213,255

; Change DoMyName to "1" and set MyName and MyPassword to flag "your" posts.
DoMyName=1
MyName=f4jonatas
MyPassword=sekuelas
MyColor=255,222,161,255

; ForumsToWatch can be any text from the title of a forum or subject of a post.
; Use "|" to separate key words / phrases.
; Some of the titles of forums are "shortened" in the .lua file. Check there.
; Set ForumsToWatch to "" to disable
ForumsToWatch= ""
; ForumsToWatch=Developers|Bugs & Suggestions|Changes|Site|Chameleon
WatchColor=244,125,67,255

[TextStyle]
X=45
Y=2R
W=370
H=17
FontColor=255,255,255,255
FontFace=Fira Sans
FontSize=10
SolidColor=0,0,0,1
StringStyle=Normal
ClipString=1
ToolTipType=1
ToolTipWidth=200
AntiAlias=1
