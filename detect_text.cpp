/**
 * Text detection implementation using Tesseract.
 * Author - Sanjeev Kumar Maheve
 **/
#include <opencv2/opencv.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <tesseract/baseapi.h>
#include <tesseract/ocrclass.h>
#include <boost/filesystem.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/property_tree/detail/xml_parser_error.hpp>
#include <boost/property_tree/ptree.hpp>
#include <boost/property_tree/xml_parser.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/iostreams/device/file_descriptor.hpp>
#include <boost/iostreams/operations.hpp>
#include <iostream>
#include <new>
#include <stack>
#include <sstream>
#include <iterator>
// include, Jsoncpp
#include <jsoncpp/json/json.h>
#include <signal.h>                             // To trap signals.
#include <stdlib.h>								// for putenv

#undef ARCHAIC_DOCUMENT_ALGORITHMS
#undef __USE_DEPRICATED_CODE__
#define RGB_FRAMES (3)                          // Assumption - PPM binary has R, G, B frame data.
#define MAX_FRAME_SIZE (1920*1080*RGB_FRAMES)   // Max size assumed as 1920x1080 for R,G, B frames
#define LINE_FEED (10)                          // Assumption - PPM header new line character
#define MIN_PPM_HDR_LEN (15)                    // Assumption - Input PPM does not \
                                                // have comments with in first 15 bytes.
#define SUBTITLES "lines"                       // JSON structure key for OCR based subtitles.
#define OCR_TEXT "text"                         // JSON structure key for any other desired text.
#define OCR_CONF "confidence"                   // JSON structure key for level of confidence
#define OCR_WIND "rect"                         // JSON structure key for window co-ordinates
#define OCR_INPUT (0)                           // OCR Monitor STDIN
#define OCR_OUTPUT (1)                          // OCR Monitor STDOUT
#define OCR_CONF_THRESHOLD (70)                 // OCR Confidence Threshold
#define MAX_ARGUMENT_LIST (2)                   // argv[1] = channel id 
#define R_MAX (200)                             // Max. value for R pixel
#define G_MAX (200)                             // Max. value for G pixel
#define B_MAX (200)                             // Max. value for B pixel
using namespace cv;
using namespace std;
using namespace tesseract;
using namespace boost::filesystem;
using boost::property_tree::ptree;

// Global variable declaration
//
static volatile bool gKeepRunning = true;

// User-defined type declaration
//
typedef struct _OCR_WINDOW {
    int left;   // x1
    int top;    // y1
    int right;  // x2
    int bottom; // y2
    string language;
    bool isSubtitles;
    bool disableRegionOfInterest;
} OCR_Window;

//****************************************************************************/
//
//  Purpose:
//
//  To convert hexadecimal data to binary.
//
//  Parameters:
//
//    Input - hexadecimal data.
//
//  Note:
//    The hexadecimal data at the input is assumed to be in lower case.
//
//****************************************************************************/
string HexToString(const string& input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();
    if (len & 1) {
        throw invalid_argument("ERROR Odd length");
    }

    string output;
    output.reserve(len / 2);
    for (size_t i = 0; i < len; i += 2) {
        char a = toupper(input[i]);
        const char* p = lower_bound(lut, lut + 16, a);
        if (*p != a) {
            cout << a << endl;
            throw invalid_argument("ERROR Not a hex digit");
        }

        char b = toupper(input[i + 1]);
        const char* q = lower_bound(lut, lut + 16, b);
        if (*q != b) {
            cout << b << endl;
            throw invalid_argument("ERROR Not a hex digit");
        }

        output.push_back(((p - lut) << 4) | (q - lut));
    }

    return output;
}

//****************************************************************************/
//
//  Purpose:
//
//  To convert hexadecimal data to binary.
//
//  Parameters:
//
//    Input - hexadecimal data.
//
//  Note:
//    The hexadecimal data at the input is assumed to be in lower case.
//
//****************************************************************************/
bool LoadOcrWindowConfig(OCR_Window& ocrWindow, path filePath)
{
    if (!exists(filePath) || !is_regular_file(filePath)) {
        cerr << "ERROR Could not read rect coordinate file - " << filePath << endl;
        return false;
    } else {
        cerr << "DEBUG Using provider configuration file - " << filePath << endl;
    }

    ptree* config = new ptree();
    if (!config) {
        cerr << "ERROR Memory allocation failure in " << __func__ << endl;
        return false;
    }
   
    try {
        read_xml(filePath.string().c_str(), *config);
    } catch (boost::property_tree::xml_parser::xml_parser_error e) {
        cerr << "DEBUG Failed to parse file path " << filePath << endl;
        cerr << "DEBUG Message: " << e.message() << endl;
        cerr << "DEBUG line: " << e.line() << endl;

        delete config;
        return false;
    }

    if (config->get<bool>("disableRegionOfInterest",false) == true) {
        cerr << "DEBUG ROI is disabled in input XML file Skipping" << endl;
        
        delete config;
        return false;
    }

    ocrWindow.left     = config->get<int>("left",0);
    ocrWindow.top      = config->get<int>("top",0);
    ocrWindow.right    = config->get<int>("right",0);
    ocrWindow.bottom   = config->get<int>("bottom",0);
    ocrWindow.language = config->get<string>("language","eng");
    ocrWindow.isSubtitles = config->get<bool>("isSubtitles",false);
    ocrWindow.disableRegionOfInterest = config->get<bool>("disableRegionOfInterest",true);

    cerr << "DEBUG left = " << ocrWindow.left << endl << \
        "DEBUG top = " << ocrWindow.top << endl << \
        "DEBUG right = " << ocrWindow.right << endl << \
        "DEBUG bottom = " << ocrWindow.bottom << endl << \
        "DEBUG language = " << ocrWindow.language << endl << \
        "DEBUG disable-roi = " << ocrWindow.disableRegionOfInterest << endl; 

    delete config;
    return true;
}

//****************************************************************************/
//
//  Purpose:
//
//  To terminal the execution of the main thread on detection signals.
//
//  Parameters:
//
//    Input, interrupt or abort signal values.
//
//****************************************************************************/
void signalHandler (int sigNum)
{
    gKeepRunning = false;
}

//****************************************************************************/
//
//  Purpose:
//
//  To trim the leading and trailing spaces of the string.
//
//  Parameters:
//
//    Input/Output, string.
//
//****************************************************************************/
inline string trim(std::string& str)
{
    size_t found = str.find_first_not_of(' ');
    if (found != string::npos) {
        str.erase(0, found);       //prefixing spaces
    }

    found = str.find_last_not_of(' ');
    if (found != string::npos) {
        str.erase(found + 1);       //surfixing spaces
    }

    return str;
}

//****************************************************************************/
//
//  Purpose:
//
//  To extract text from inside the video using Tesseract open source.
//
//  Parameters:
//
//    Input, command line parameters from the user.
//
//****************************************************************************/
int main (int argc, char **argv) {
    signal(SIGINT, signalHandler);
    signal(SIGABRT, signalHandler);

    OCR_Window ocrWindow;
    vector<unsigned char> vectorBuffer;
    vector<string> output;

    string filePath("<nothing>");
    string jsonString("<nothing>");
    string prevOcrOutput("<nothing>");
    
    Json::Value inputData;
    Json::Value outputData;
    Json::Value line_;
    Json::Value lines;
    Json::Value rectCoordinates;
    Json::Reader jsonReader;
    Json::FastWriter jsonWriter;

    // Ensure ROI filename at command line.
    // 
    if (argc < MAX_ARGUMENT_LIST) {
        cerr <<"DEBUG No channel id specified at command-line." << endl;
        filePath.clear();
    } else {
        filePath = argv[1];
    }
    
    // Declaration to store Region Of Interest (ROI) information (if any, else
    // full frame is a ROI).
    //
    Rect roi;
    if (filePath.empty() || LoadOcrWindowConfig (ocrWindow, filePath+".xml") != true) {
        cerr << "ERROR Couln't read the rect coordinates from supplied input." << endl;
        cerr << "WARN OCR will be done for the entire frame." << endl;
    } else {
        roi.x = ocrWindow.left;
        roi.y = ocrWindow.top;
        roi.width = (ocrWindow.right - ocrWindow.left);
        roi.height = (ocrWindow.bottom - ocrWindow.top);
    
        cerr << "DEBUG x = " << roi.x << endl << \
            "DEBUG y = " << roi.y << endl << \
            "DEBUG width = " << roi.width << endl << \
            "DEBUG height = " << roi.height << endl << \
            "DEBUG language = " << ocrWindow.language << endl; 
    }

    // Initialise the environment variable to point to Tesseract data and
    // initialize the language (default to) english.
    TessBaseAPI tesseractApi;
    putenv("TESSDATA_PREFIX=./tesseract-ocr/");
    
    if (!(ocrWindow.language).empty()) {
        cerr << "DEBUG language chosen = " << ocrWindow.language << endl;
        tesseractApi.Init("./tesseract-ocr/", (ocrWindow.language).c_str());
    } else {
        cerr << "DEBUG language chosen = english" << endl;
        tesseractApi.Init("./tesseract-ocr/", "eng");
    }

    while (gKeepRunning &&  getline(cin, jsonString, '\n') ) { 
        // ROI = default frame size
        //
        if (!jsonReader.parse(jsonString, inputData)) {
            cerr << "DEBUG Invalid JSON - Ignoring..." << endl;
            continue;
        }

        // Get the frame data from the input JSON.
		unsigned int width = inputData["data"]["frame"]["width"].asUInt();
		unsigned int height = inputData["data"]["frame"]["height"].asUInt();
		unsigned int idx = inputData["data"]["frame"]["index"].asUInt();
		string hexFrame = inputData["images"][idx]["hex"].asString();
        
        if((argc < MAX_ARGUMENT_LIST) || 
           !((roi.x >= 0 && roi.x < width) &&
             (roi.y >= 0 && roi.y < height) &&
             ((roi.x + roi.width) >= 0 && (roi.x + roi.width) < width) && 
             ((roi.y + roi.height) >= 0 && (roi.y + roi.height) < height))){
            roi.x = 0;
            roi.y = 0;
            roi.width = width;
            roi.height = height;
	
            cerr << "DEBUG x = " << roi.x << endl << \
                "DEBUG y = " << roi.y << endl << \
                "DEBUG width = " << roi.width << endl << \
                "DEBUG height = " << roi.height << endl; 
        }

        // Read the frame into as string type.
        cerr << "DEBUG HexToString" << endl;
        string binaryFrame = HexToString (hexFrame);
        
        cerr << "DEBUG vectorBuffer.assign" << endl;
        vectorBuffer.assign(binaryFrame.c_str(), binaryFrame.c_str() + binaryFrame.length());

        cerr << "DEBUG Decoding the input image." << endl;
        Mat frame_roi = ((Mat)(imdecode(Mat(vectorBuffer), CV_LOAD_IMAGE_COLOR)))(roi);
       
        // compute the sum of positive matrix elements, optimized variant
        int cols = frame_roi.cols, rows = frame_roi.rows;
        if(frame_roi.isContinuous())
        {
            cols *= rows;
            rows = 1;
        }
        
        cerr << "Cols = " << cols <<endl;
        cerr << "Rows = " << rows <<endl;
        
        uint8_t* pixelPtr = (uint8_t*)frame_roi.data;
        int cn = frame_roi.channels();
        for(int i = 0; (i < frame_roi.rows) && (true == ocrWindow.isSubtitles); i++) {
            for(int j = 0; j < frame_roi.cols; j++) {
                Scalar_<uint8_t> bgrPixel;
                bgrPixel.val[0] = pixelPtr[i*frame_roi.cols*cn + j*cn + 0]; // B
                bgrPixel.val[1] = pixelPtr[i*frame_roi.cols*cn + j*cn + 1]; // G
                bgrPixel.val[2] = pixelPtr[i*frame_roi.cols*cn + j*cn + 2]; // R

                // do something with BGR values...
                //
                if (!(bgrPixel.val[0] >= B_MAX && bgrPixel.val[1] >= G_MAX && bgrPixel.val[2] >= R_MAX)) {
                    pixelPtr[i*frame_roi.cols*cn + j*cn + 0] = 0; // B
                    pixelPtr[i*frame_roi.cols*cn + j*cn + 1] = 0; // G
                    pixelPtr[i*frame_roi.cols*cn + j*cn + 2] = 0; // R
                }
            }
        }
#if defined __USE_DEPRICATED_CODE__
        namedWindow("Frame from video", CV_WINDOW_AUTOSIZE);
        if (!(frame_roi.empty())) {
            imshow("jpg", frame_roi);
        } 
        waitKey(0);
#endif
        // Clear the vector content after use.
        //
        cerr << "DEBUG vectorBuffer.clear" << endl;
        vectorBuffer.clear();

        if (frame_roi.empty()) {
            cerr << "FATAL Empty Frame or end of Stream!" << endl;
            break;
        }
        
        tesseractApi.Init(argv[0], "eng", OEM_DEFAULT);
        tesseractApi.SetPageSegMode(PSM_AUTO);
        tesseractApi.SetImage((uchar*)frame_roi.data, \
                              frame_roi.size().width, \
                              frame_roi.size().height, \
                              frame_roi.channels(), \
                              frame_roi.step1());
        tesseractApi.Recognize(0);
        char* ocrText = tesseractApi.GetUTF8Text();
        output.push_back(ocrText);
        int confidence = tesseractApi.MeanTextConf();

        for (size_t i=0; (i < output.size()) && \
             (confidence > OCR_CONF_THRESHOLD); i++) {
            // Trim the leading and the trailing spaces.
            //
            trim (output[i]);

            // Skip if the input is empty
            //
            if (true == output[i].empty() || !output[i].length()) {
                cerr << "DEBUG Empty ocr-text, skipping." << endl;
                continue;
            }

            // Replace extra newline at the end of the string.
            while (output[i][output[i].length()-1] == '\n' || 
                   output[i][output[i].length()-1] == ' ') {
                output[i].erase(output[i].length()-1);
                cerr << "DEBUG Deleting new-line at the end of the ocr-text." << endl;
            }

            if (true == ocrWindow.isSubtitles) {
                // Clear the previously accumulated data.
                //
                lines.clear();

                // Placeholder for the extracted text for post-processing (i.e.
                // trimming leading/trailing spaces etc.)
                string text_;
                size_t lStartIdx = 0;
                size_t lEndIdx = output[i].find_first_of('\n');
                while (lEndIdx != string::npos)
                {
                    text_ = output[i].substr(lStartIdx, lEndIdx-lStartIdx); trim(text_);
                    if (text_.length()) {
                        line_[OCR_TEXT] = text_;
                        lines.append(line_);
                    }

                    lStartIdx = (lEndIdx + 1);
                    lEndIdx = output[i].find_first_of('\n', lStartIdx);
                }
               
                text_ = output[i].substr(lStartIdx, output[i].length()-lStartIdx);
                trim(text_); // trim the leading and trailing spaces (if any).
                if (text_.length()) {
                    line_[OCR_TEXT] = text_;
                    lines.append(line_);
                }

                if (!lines.empty()) {
                    outputData[SUBTITLES] = lines;
                }

                text_.clear();
            } else {
                // Remove any leading new-line or spaces.
                size_t lEndIdx = output[i].find_first_of('\n');
                while(lEndIdx == 0) {
                    output[i] = output[i].substr(1);
                    trim(output[i]);

                    lEndIdx = output[i].find_first_of('\n');
                }

                // Fill key-value information into JSON object
                if (!output[i].empty()) {
                    outputData[OCR_TEXT] = output[i].c_str();
                } else {
                    continue;
                }
            }

            rectCoordinates["left"] = ocrWindow.left;
            rectCoordinates["top"] = ocrWindow.top;
            rectCoordinates["right"] = ocrWindow.right;
            rectCoordinates["bottom"] = ocrWindow.bottom;
            outputData[OCR_CONF] = confidence;
            outputData[OCR_WIND] = rectCoordinates;
            string currOcrOutput = jsonWriter.write(outputData);
            
            if (!currOcrOutput.empty() && prevOcrOutput.compare(currOcrOutput) != 0) {
                ssize_t jsonSize = write(OCR_OUTPUT, currOcrOutput.c_str(), currOcrOutput.length());

                // Keep a copy to refer in next iteration.
                //
                prevOcrOutput.clear();
                prevOcrOutput = currOcrOutput;
            } else {
                cerr << "DEBUG Prev = " << prevOcrOutput << "DEBUG New = " << currOcrOutput << endl;
            }
        }
        
        cerr << "DEBUG Confidence  = " << confidence << " (Only > " << OCR_CONF_THRESHOLD << " is posted)" << endl;
        delete [] ocrText;

        // Clear vector after producing the output.
        output.clear();
    }

    tesseractApi.Clear();
    tesseractApi.End();
    
    cerr << "OCR stopped..." << endl;
    return 1;
}
