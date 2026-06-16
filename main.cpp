#include <opencv2/opencv.hpp>
#include <iostream>
#include <chrono>      // Required for execution time profiling
#include <filesystem>  // Required for local file validation
#include <string>
#include <vector>

using namespace std;
using namespace cv;
using namespace chrono;
using namespace filesystem;

int main(int argc, char** argv) {

    // ------------------------------------------------------------------ //
    //  1. INPUT & COMMAND-LINE ARGUMENT PARSING
    // ------------------------------------------------------------------ //

    string video_path = "";
    const string default_video_path = "attempt.mov";
    bool raw_output = false;

    // Parse command-line arguments to determine operational modes and file paths
    for (int i = 1; i < argc; ++i) {
        string arg = argv[i];
        if (arg == "--raw") {
            raw_output = true; // Enable binary data stream to stdout
        } else {
            video_path = arg;  // Assign the provided video path
        }
    }

    // Fallback logic if no target path is specified by the user
    if (video_path.empty()) {
        if (exists(default_video_path)) {
            video_path = default_video_path;
            if (!raw_output) {
                cout << "[INFO] Auto-loading local default video: '" << video_path << "'\n";
            }
        } else {
            cerr << "Usage: " << argv[0] << " <video_path> [--raw]\n";
            return -1;
        }
    }

    // Initialize the video capture object using the FFMPEG backend
    VideoCapture cap(video_path, CAP_FFMPEG);
    if (!cap.isOpened()) {
        cerr << "\n[ERROR] Cannot open video file: '" << video_path << "'\n";
        return -1;
    }

    // ------------------------------------------------------------------ //
    //  2. EQUIRECTANGULAR TO CUBEMAP PROJECTION (6 FACES)
    // ------------------------------------------------------------------ //

    // Retrieve the native resolution of the input spherical video
    int video_width  = cap.get(CAP_PROP_FRAME_WIDTH);
    int video_height = cap.get(CAP_PROP_FRAME_HEIGHT);

    const int OUTPUT_FACE_DIM = 1024; // Resolution of a single cubemap face (1024x1024)
    const int NUM_FACES = 6;

    Mat frame;
    Mat map_x[NUM_FACES], map_y[NUM_FACES]; // Lookup tables for the remapping operation

    // Allocate memory for the transformation maps
    for (int i = 0; i < NUM_FACES; ++i) {
        map_x[i] = Mat(OUTPUT_FACE_DIM, OUTPUT_FACE_DIM, CV_32F);
        map_y[i] = Mat(OUTPUT_FACE_DIM, OUTPUT_FACE_DIM, CV_32F);
    }

    // Pre-compute the backward mapping lookup tables for all six cubemap faces
    for (int face = 0; face < NUM_FACES; ++face) {
        for (int i = 0; i < OUTPUT_FACE_DIM; ++i) {
            for (int j = 0; j < OUTPUT_FACE_DIM; ++j) {
                
                // Normalize current cubemap pixel coordinates to the [-1, 1] range
                float x = (j / (float)OUTPUT_FACE_DIM) * 2.0f - 1.0f;
                float y = (i / (float)OUTPUT_FACE_DIM) * 2.0f - 1.0f;

                float X, Y, Z; // 3D Cartesian coordinates on the unit sphere

                // Map the 2D cubemap coordinates to 3D directional vectors
                switch (face) {
                    case 0: X = -1.0f; Y = y;     Z = x;     break; // Left Face
                    case 1: X = x;     Y = y;     Z = 1.0f;  break; // Front Face
                    case 2: X = 1.0f;  Y = y;     Z = -x;    break; // Right Face
                    case 3: X = -x;    Y = y;     Z = -1.0f; break; // Back Face
                    case 4: X = x;     Y = -1.0f; Z = y;     break; // Top Face
                    case 5: X = x;     Y = 1.0f;  Z = -y;    break; // Bottom Face
                }

                // Convert the 3D Cartesian coordinates back to spherical coordinates
                float theta = atan2(X, Z);
                float phi   = asin(Y / sqrt(X*X + Y*Y + Z*Z));

                // Normalize the spherical coordinates to the equirectangular UV space [0, 1]
                float u = (theta / (2.0f * CV_PI)) + 0.5f;
                float v = (phi   / CV_PI)          + 0.5f;

                // Scale by the input video dimensions and store in the lookup tables
                map_x[face].at<float>(i, j) = u * video_width;
                map_y[face].at<float>(i, j) = v * video_height;
            }
        }
    }

    // ------------------------------------------------------------------ //
    //  3. OUTPUT ALLOCATION: HORIZONTAL CROSS LAYOUT (4 Columns x 3 Rows)
    // ------------------------------------------------------------------ //

    // Allocate a 4x3 master grid initialized with zeros (black background).
    // Target Layout Architecture:
    // [ Empty ] [  Top   ] [ Empty  ] [ Empty ]
    // [ Left  ] [ Front  ] [ Right  ] [ Back  ]
    // [ Empty ] [ Bottom ] [ Empty  ] [ Empty ]
    
    Mat panoramic_cross = Mat::zeros(OUTPUT_FACE_DIM * 3, OUTPUT_FACE_DIM * 4, CV_8UC3);

    // Define Region of Interest (ROI) headers pointing to the exact geometric 
    // sectors within the master cross matrix. This avoids deep copying data later.
    Mat top_roi    = panoramic_cross(Rect(OUTPUT_FACE_DIM * 1, 0,                   OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));
    
    Mat left_roi   = panoramic_cross(Rect(0,                   OUTPUT_FACE_DIM,     OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));
    Mat front_roi  = panoramic_cross(Rect(OUTPUT_FACE_DIM * 1, OUTPUT_FACE_DIM,     OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));
    Mat right_roi  = panoramic_cross(Rect(OUTPUT_FACE_DIM * 2, OUTPUT_FACE_DIM,     OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));
    Mat back_roi   = panoramic_cross(Rect(OUTPUT_FACE_DIM * 3, OUTPUT_FACE_DIM,     OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));
    
    Mat bottom_roi = panoramic_cross(Rect(OUTPUT_FACE_DIM * 1, OUTPUT_FACE_DIM * 2, OUTPUT_FACE_DIM, OUTPUT_FACE_DIM));

    // ------------------------------------------------------------------ //
    //  4. MAIN PROCESSING LOOP
    // ------------------------------------------------------------------ //

    while (cap.read(frame)) {

#ifdef ENABLE_PROFILING
        auto start = high_resolution_clock::now();
#endif

        // Apply the pre-computed remapping to warp the equirectangular frame 
        // directly into the respective cross sections. BORDER_WRAP preserves toroidal continuity.
        remap(frame, left_roi,   map_x[0], map_y[0], INTER_LINEAR, BORDER_WRAP);
        remap(frame, front_roi,  map_x[1], map_y[1], INTER_LINEAR, BORDER_WRAP);
        remap(frame, right_roi,  map_x[2], map_y[2], INTER_LINEAR, BORDER_WRAP);
        remap(frame, back_roi,   map_x[3], map_y[3], INTER_LINEAR, BORDER_WRAP);
        remap(frame, top_roi,    map_x[4], map_y[4], INTER_LINEAR, BORDER_WRAP);
        remap(frame, bottom_roi, map_x[5], map_y[5], INTER_LINEAR, BORDER_WRAP);

#ifdef ENABLE_PROFILING
        auto end = high_resolution_clock::now();
        duration<double, milli> elapsed = end - start;
        if (!raw_output) {
            cout << "[PROFILING] Frame processing time: " << elapsed.count() << " ms\r" << flush;
        }
#endif

        if (raw_output) {
            // Stream the raw, uncompressed cross layout buffer directly to standard output (stdout).
            // Ideal for piping data into subsequent processing stages (e.g., Python, FFmpeg).
            cout.write(reinterpret_cast<const char*>(panoramic_cross.data),
                       panoramic_cross.total() * panoramic_cross.elemSize());
        } else {
            // Downscale the preview image to fit standard monitor resolutions safely.
            // The native computational resolution remains 4096x3072.
            namedWindow("Cubemap Cross Preview", WINDOW_NORMAL);
            Mat preview;
            resize(panoramic_cross, preview, Size(), 0.5, 0.5, INTER_LINEAR); 
            imshow("Cubemap Cross Preview", preview);
            
            // Wait for 1 ms and intercept the 'q' key to gracefully terminate the loop
            if ((waitKey(1) & 0xFF) == 'q') {
                break;
            }
        }
    }

    // ------------------------------------------------------------------ //
    //  5. RESOURCE CLEANUP AND TERMINATION
    // ------------------------------------------------------------------ //

    cap.release();
    if (!raw_output) {
        destroyAllWindows();
        cout << "\n[INFO] Processing successfully finished.\n";
    }
}