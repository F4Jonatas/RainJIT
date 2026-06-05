
[InfoIndex2]
Measure=WebParser
URL=https://www.cnbc.com/quotes/#Symbol2#
; URL=file://stockfile2.xml
UserAgent=#MyUserAgent#
Header=Accept: text/html
Header2=Accept_Encoding: gzip, deflate, br
Header3=Accept_Language: en-US
Header4=Connection: close
Header5=Dnt: 1
Header6=Host: www.CNBC.com
Header7=Referer: https://wwww.google.com
RegExp=#ExpQuote#
Substitute= "":"   loading..."
StringIndex=1
UpdateRate=#UpdateQuotes#

[mIndex2_Price]
Measure=WebParser
Url=[InfoIndex2]
StringIndex=2
Substitute=",":""

[mIndex2_Change]
Measure=WebParser
Url=[InfoIndex2]
StringIndex=3
Substitute="UNCH":"0"
IfCondition=(#CURRENTSECTION# > 0)
IfTrueAction=[!SetVariable "PriceColor2" "#ColorUp#"]  [!SetOption "UpDownImage_2" "ImageName" "Up.png"]
IfCondition2=(#CURRENTSECTION# = 0)
IfTrueAction2=[!SetVariable "PriceColor2" "#ColorSteady#"] [!SetOption "UpDownImage_2" "ImageName" "Steady.png"]
IfCondition3=(#CURRENTSECTION# <0 )
IfTrueAction3=[!SetVariable "PriceColor2" "#ColorDown#"] [!SetOption "UpDownImage_2" "ImageName" "Down.png"]
OnChangeAction=[!ShowMeterGroup PriceGroup2] [!UpdateMeterGroup PriceGroup2] [!Redraw]
DynamicVariables=1

[cDayLossGain2]
Measure=Calc
Formula= ( mIndex2_Change * #Units2#  )
RegExpSubstitute=1
Substitute="\-":"","^(\d{1,3})(\d{3})(\d{3})$":"\1,\2,\3","^(\d{1,3})(\d{3})$":"\1,\2"
DynamicVariables=1

[cChangeFromBuyPrice2]
Measure=Calc
Formula=  ( [mIndex2_Price] - #BuyPrice2# )
IfCondition=(#CURRENTSECTION# > 0)
IfTrueAction=[!SetVariable "OverallColor2" "#ColorUp#"]
IfCondition2=(#CURRENTSECTION# = 0)
IfTrueAction2=[!SetVariable "OverallColor2" "#ColorSteady#"]
IfCondition3=(#CURRENTSECTION# <0 )
IfTrueAction3=[!SetVariable "OverallColor2" "#ColorDown#"]
OnChangeAction=[!ShowMeterGroup OverallGroup2] [!UpdateMeterGroup OverallGroup2] [!Redraw]
DynamicVariables=1

[cFromBuyPer2]
Measure=Calc
Formula=  ( cChangeFromBuyPrice2 / #BuyPrice2# ) * 100

[cGainLoss2]
Measure=Calc
Formula= ( #Units2# * [cChangeFromBuyPrice2] )
RegExpSubstitute=1
Substitute="\-":"","^(\d{1,3})(\d{3})(\d{3})$":"\1,\2,\3","^(\d{1,3})(\d{3})$":"\1,\2"
DynamicVariables=1

[cInvestment2]
Measure=Calc
Formula= ( #Units2# * #BuyPrice2# )
RegExpSubstitute=1
Substitute="\-":"","^(\d{1,3})(\d{3})(\d{3})$":"\1,\2,\3","^(\d{1,3})(\d{3})$":"\1,\2"
UpdateDivider=-1

[mIndex2_ChangePer]
Measure=Calc
Formula=( cDayLossGain2 / cInvestment2 * 100 )

;-------------- meters

[LabelIndex2]
Meter=STRING
MeasureName=InfoIndex2
MeterStyle=sTextLeft | sColorSetGray
LeftMouseUpAction=#Site##Symbol2#
ToolTipText="Show [InfoIndex2] on CNBC.com"
X=#Col1XPos#
Y=#RowYspacing#r

[InvestmentAmount2]
Meter=STRING
MeasureName=cInvestment2
MeterStyle= sTextRight | sColorSetGray
x=#Col2Xpos#
UpdateDivider=-1

[LabelPrice2]
Meter=STRING
MeterStyle= sTextRight
FontColor=#PriceColor2#
x=#Col3Xpos#
Text=[&mFormatValue:FormatPrice( [&mIndex2_Price] ) ]
UpdateDivider=-1
Group=PriceGroup2
Hidden=1

[UpDownImage_2]
Meter=Image
ImageName=Steady.png
x=#Col4Xpos#
y=-6r
Group=Images

[DayValueChange-2]
Meter=STRING
MeasureName=cDayLossGain2
MeterStyle= sTextRight
FontColor=#PriceColor2#
x=#Col5Xpos#
y=6r
Group=PriceGroup2
Hidden=1

[LabelIndex2_ChangePer]
Meter=STRING
MeterStyle=sTextRight
FontColor=#PriceColor2#
x=#Col6Xpos#
Text=([mIndex2_ChangePer:/1,#PerDecPlace#]%)
Group=PriceGroup2
Hidden=1

[OverallGainLoss2]
Meter=STRING
MeterStyle= sTextRight
FontColor=#OverallColor2#
x=(#Col8XPos#)
MeasureName=cGainLoss2
Group=OverallGroup2
Hidden=1

[ChangefromBuyPer2]
Meter=STRING
MeterStyle= sTextRight
FontColor=#OverallColor2#
x=#Col9XPos#
Text=([cFromBuyPer2:/1,#PerDecPlace#]%)
Group=OverallGroup2
Hidden=1

; ------------------------------------
