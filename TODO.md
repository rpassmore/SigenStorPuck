
# Current Tasks
We need add to add support for a new page in our sigenstorPuck app, the page will display the days import and export electricity rates:

# Settings & UI
- The user should configure their import and export tariffs using the we based settings UI.
- The settings UI should be updated to add a number of drop downs. A drop down containing the available import tariffs. Another drop down to configure the export tariffs the available options in these import and export drop downs should be obtained from Octopus Energy via the internet. 
- The settings UI also needs an entry where the user can configure the time at which the tariff rates for the day will be fetched from the internet. 

# New Page
- Once a day the electricity rates will be fetched for the users selected tariffs from the internet this fetch will performed at the users configured time. The data fetched is likely to be in half hourly values for the day.  
- Page will display the days import and export electricity rates in a graph, using the existing graph library in the code base. The graph will plot import and export rates (import in green/ export in amber) on the Y axis with time (in half hour divisions) on the X axis 
- The graph of the days electrictiy rates will feature a verical line that indicates the current time position across the graph.
- An textual indication of the current import and export rate (in pence/kWh) will be displayed on page. 




# Future work - Still To-Do
- simulate absent hardware buttons with touch/gestures
- Font rendering is not always as clear as it could be on smaller/lighter text.
- Data/glitches drop outs
- Values switch from import/export, charge/discharge regularly when at very low magnitudes 0.01 etc.
- Battery SOC guage on home screen move to battery icon.
- Tariff page.

# Done
- Touch co-ordinates are not correct, however the swipe genstures do work. Logs show lots of warnings regarding out of range ords.
- Backlight anything other than max seems to blank/black the screen
- Display guage of import/export on home screen
- Swap current power consumption/generation values and daily totals position in UI pages.
- 3D print stand + add models to repo. Bone White (11103)
https://www.ebay.co.uk/itm/136112296704?_trkparms=itmf%3D1%26aid%3D1110006%26rkt%3D10%26algo%3DHOMESPLICE.SIM%26pid%3D101506%26mech%3D1%26algv%3DSimOrganicCassiniWithToraRecalls%26pmt%3D0%26amclksrc%3DITM%26sd%3D278364311104%26sid%3DAQALAAAAEJSVUVfDTPBaZKjmFxNUnyE%3D%26itm%3D136112296704%26noa%3D1%26brand%3DUnbranded%26asc%3D343028%26ao%3D1%26rk%3D3%26mehot%3Dnone%26lsid%3D3%26meid%3D57ed1a7d898942dfa0f9bc7c3fcbc358%26pg%3D4481478&_trksid=p4481478.c101506.m1851