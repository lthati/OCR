Compilation
===========

Option-1:
---------

$ g++ -o extractOCR detect_text.cpp -I/usr/include/tesseract -L/usr/lib -ltesseract -lboost_filesystem -lboost_system -ljsoncpp -lopencv_core -lopencv_imgproc -lopencv_highgui

NOTE - Include files and libraries are assumed to be installed under standard searchable path. Else specify library path accordingly.

For Example:
/usr/bin/ld: cannot find -lboost_filesystem
/usr/bin/ld: cannot find -lboost_system
collect2: ld returned 1 exit status

$ g++ -o extractOCR detect_text.cpp -I/usr/include/tesseract -L/usr/lib -ltesseract -L/home/sanjeevma/Projects/lunar/Chandra/lib -lboost_filesystem -lboost_system -ljsoncpp -lopencv_core -lopencv_imgproc -lopencv_highgui

Option-2:
---------

$ make

NOTE - Include and Library path should be set accordingly under Makefile if not available under default installation path.
