
# Hardware Description
Summary: Guition ESP32-4848S040 

Working Configuration Rules:
Feature	Setting
Display	ST7701, 480×480, RGB parallel, init table from sand1812
PCLK	Polarity inverted (active_neg=1)
Touch I2C	SDA=19, SCL=45 (NOT 20 — GPIO20 is display's G1 line)
Touch IC	GT911 at address 0x5D, point register at 0x814F
Touch Byte Order	Little-endian (standard datasheet convention)
Touch I2C Pattern	STOP-then-separate-request (not repeated-start)
Backlight	PWM on GPIO38
No PMIC	Battery indicator disabled gracefully
No Buttons	Stubs; swipe gestures still work


# Future work - Still To-Do
- simulate absent hardware buttons with touch/gestures
- Font rendering is not always as clear as it could be on smaller/lighter text.
- Data/glitches drop outs
- Values switch from import/export, charge/discharge regularly when at very low magnitudes 0.01 etc.
- Battery SOC guage on home screen move to battery icon.

# Done
- Touch co-ordinates are not correct, however the swipe genstures do work. Logs show lots of warnings regarding out of range ords.
- Backlight anything other than max seems to blank/black the screen
- Display guage of import/export on home screen
- Swap current power consumption/generation values and daily totals position in UI pages.
- 3D print stand + add models to repo. Bone White (11103)
https://www.ebay.co.uk/itm/136112296704?_trkparms=itmf%3D1%26aid%3D1110006%26rkt%3D10%26algo%3DHOMESPLICE.SIM%26pid%3D101506%26mech%3D1%26algv%3DSimOrganicCassiniWithToraRecalls%26pmt%3D0%26amclksrc%3DITM%26sd%3D278364311104%26sid%3DAQALAAAAEJSVUVfDTPBaZKjmFxNUnyE%3D%26itm%3D136112296704%26noa%3D1%26brand%3DUnbranded%26asc%3D343028%26ao%3D1%26rk%3D3%26mehot%3Dnone%26lsid%3D3%26meid%3D57ed1a7d898942dfa0f9bc7c3fcbc358%26pg%3D4481478&_trksid=p4481478.c101506.m1851
- Tariff page.

