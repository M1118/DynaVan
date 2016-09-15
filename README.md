# DynaVan

A speed and distance measuring device for a 4mm scale model railway.

Uses an interruption sensor to sense wheel rotation and converts that
to speed and distance information. The information is presented via
a website hosted on the ESP-01 module within the van. This gives
real time speed and distance data as the vehicle moves through your
layout. The speed and distance are converted to scale speed and
distance, allowing you to get a feel for the speed of your trains.

In this example power is supplied via an onboard battery, but could
also be picked up from the tracks if required, however care needs
to be taken to not introduce drag that could cause wheel slip and
inaccurate readings.
