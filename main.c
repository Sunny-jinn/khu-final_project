#include <stdio.h>
#include <CoreGraphics/CoreGraphics.h>
#include <ApplicationServices/ApplicationServices.h>
#include "portaudio.h"
#include <math.h>
#include <time.h>

/*
마우스가 움직일때마다 mouse callback함수가 호출이됨 반복문에서
마우스가 멈추면 callback x
callback -> 소리를 재생하거든. 소리가 남아있는걸 마우스가 멈추자마자 바로 소리를 짤라야되는데
*/

#define SAMPLE_RATE   (44100)
#define TABLE_SIZE    (100)

typedef struct {
    float sine[TABLE_SIZE];
    float phase;
} paTestData;

bool shouldExit = false;
bool mouseMoved = false;
bool mouseIsMoving = false;

paTestData data;

float prevX, prevY;
float num, prevBrightness = 0.0,brightness, brightnessDiff;
float newSpeed = 0;

CGImageRef capturedImage = NULL;
size_t captureWidth = 0;
size_t captureHeight = 0;

CGPoint lastPrintPoint = {0, 0};

float randomNoise(float speed, float frequency) {
    float noiseAmplitude = 0.4 * (speed + frequency);
    return noiseAmplitude;
}

void generateSoundFromPixelValues(const UInt8 *pixelData, size_t pixelIndex, float speed) {
    UInt8 red = pixelData[pixelIndex + 2];
    UInt8 green = pixelData[pixelIndex + 1];
    UInt8 blue = pixelData[pixelIndex];

    float brightness = (0.299 * red + 0.587 * green + 0.114 * blue) / 255.0;

    float brightnessDiff = fabs(brightness - prevBrightness);

    if (brightnessDiff > 0.1) {
        float amplitude = brightnessDiff;
        float frequency = 10.0 * brightnessDiff;

        if (speed > 10) {
            newSpeed = 2;
        } else if (speed > 1) {
            newSpeed = 1;
        } else {
            newSpeed = 0;
        }
        time_t lastMouseMove = time(NULL);




        float *out = data.sine;
        unsigned long i;
        for (i = 0; i < TABLE_SIZE; i++) {
            *out++ = (float)((amplitude * 2) * sin(((double)i / (double)TABLE_SIZE) * M_PI * 0.8 * frequency * newSpeed)) + randomNoise(newSpeed, frequency);
        }
        data.phase = 100;
    } else {
        float *out = data.sine;
        *out++ = 0;
    }

    prevBrightness = brightness;
}

void interpolateAndPrintPixels(CGPoint startPoint, CGPoint endPoint, const UInt8 *pixelData, size_t captureWidth, float speed) {
    int x0 = (int)startPoint.x  * 2;
    int y0 = (int)startPoint.y * 2;
    int x1 = (int)endPoint.x * 2;
    int y1 = (int)endPoint.y * 2;

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);

    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;

    int err = dx - dy;

    while (1) {
        size_t pixelIndex = (y0 * captureWidth + x0) * 4;

        generateSoundFromPixelValues(pixelData, pixelIndex, speed);

        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

struct timespec last_event_time, current_event_time;
double event_interval;

CGEventRef mouseCallback(CGEventTapProxy proxy, CGEventType type, CGEventRef event, void *userInfo) {
    clock_gettime(CLOCK_MONOTONIC, &current_event_time);
    event_interval = (current_event_time.tv_sec - last_event_time.tv_sec) + (current_event_time.tv_nsec - last_event_time.tv_nsec) / 1e9;
    last_event_time = current_event_time;

    if (type == kCGEventLeftMouseDown) {
        shouldExit = true;
    } else if (type == kCGEventMouseMoved || true) {
        CGPoint cursorPoint = CGEventGetLocation(event);
        if (capturedImage == NULL) {
            CGDirectDisplayID displayID = CGMainDisplayID();
            capturedImage = CGDisplayCreateImage(displayID);
            if (capturedImage == NULL) {
                fprintf(stderr, "Error: Cannot capture screen.\n");
                return NULL;
            }
            captureWidth = CGImageGetWidth(capturedImage);
            captureHeight = CGImageGetHeight(capturedImage);
        }

        CFDataRef data2 = CGDataProviderCopyData(CGImageGetDataProvider(capturedImage));
        const UInt8 *pixelData = CFDataGetBytePtr(data2);

        float speed = fabs((cursorPoint.x * 2 - prevX) + fabs(cursorPoint.y * 2 - prevY));

        interpolateAndPrintPixels(lastPrintPoint, cursorPoint, pixelData, captureWidth, speed);
        lastPrintPoint = cursorPoint;

        prevX = cursorPoint.x * 2;
        prevY = cursorPoint.y * 2;

        mouseMoved = true;
        mouseIsMoving = true;
    }
    return event;
}

static int patestCallback(const void *inputBuffer, void *outputBuffer,
                          unsigned long framesPerBuffer,
                          const PaStreamCallbackTimeInfo *timeInfo,
                          PaStreamCallbackFlags statusFlags,
                          void *userData) {
    float *out = (float *)outputBuffer;
    unsigned long i;
    int finished = 0;
    (void)inputBuffer;


    for (i = 0; i < framesPerBuffer ; i++) {
        if (mouseIsMoving) {
            *out++ = data.sine[(int)data.phase];
            data.phase = 100;
            if (data.phase >= TABLE_SIZE) data.phase = 0;
        } else {
            *out++ = 0.0f;
            data.phase = 0;
        }
    }

    return finished;
}

int main() {
    PaStreamParameters outputParameters;
    PaStream *stream;
    PaError err;

    srand(time(NULL));

    err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "PortAudio initialization failed: %s\n", Pa_GetErrorText(err));
        return 1;
    }

    CFMachPortRef eventTap;
    CFRunLoopSourceRef runLoopSource;

    eventTap = CGEventTapCreate(
            kCGSessionEventTap,
            kCGHeadInsertEventTap,
            kCGEventTapOptionListenOnly, //kCGEventTapOptionDefault보다 kCGEventTapOptionListenOnly가 빠름
            CGEventMaskBit(kCGEventLeftMouseDown) | CGEventMaskBit(kCGEventMouseMoved),
            mouseCallback,
            NULL
    );

    if (!eventTap) {
        fprintf(stderr, "Failed to create event tap\n");
        Pa_Terminate();
        return 1;
    }

    runLoopSource = CFMachPortCreateRunLoopSource(
            kCFAllocatorDefault,
            eventTap,
            0
    );

    if (!runLoopSource) {
        fprintf(stderr, "Failed to create run loop source\n");
        CFRelease(eventTap);
        Pa_Terminate();
        return 1;
    }

    CFRunLoopAddSource(
            CFRunLoopGetCurrent(),
            runLoopSource,
            kCFRunLoopCommonModes
    );

    outputParameters.device = Pa_GetDefaultOutputDevice();
    outputParameters.channelCount = 1;
    outputParameters.sampleFormat = paFloat32;
    outputParameters.suggestedLatency = Pa_GetDeviceInfo(outputParameters.device)->defaultLowOutputLatency;
    outputParameters.hostApiSpecificStreamInfo = NULL;

    err = Pa_OpenStream(
            &stream,
            NULL,
            &outputParameters,
            SAMPLE_RATE,
            1,
            paClipOff,
            patestCallback,
            NULL
    );



    if (err != paNoError) {
        fprintf(stderr, "PortAudio stream open failed: %s\n", Pa_GetErrorText(err));
        CFRelease(runLoopSource);
        CFRelease(eventTap);
        Pa_Terminate();
        return 1;
    }

    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio stream start failed: %s\n", Pa_GetErrorText(err));
        Pa_CloseStream(stream);
        CFRelease(runLoopSource);
        CFRelease(eventTap);
        Pa_Terminate();
        return 1;
    }

    printf("Listening for mouse movement. Press left mouse button to exit.\n");

    time_t lastMouseMove = time(NULL);
    bool mouseWasMoving = false;

    while (!shouldExit) {
        CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.000000000001, false);
        if (mouseMoved) {
            lastMouseMove = time(NULL);
            mouseMoved = false;
            mouseWasMoving = true;
            mouseIsMoving = true;
        } else {
            if (time(NULL) != lastMouseMove) {
                if (mouseWasMoving) {
                    mouseWasMoving = false;
                    mouseIsMoving = false;
                }
                memset(data.sine, 0, sizeof(data.sine));
            }
        }
    }

    err = Pa_StopStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "PortAudio stream stop failed: %s\n", Pa_GetErrorText(err));
    }

    Pa_CloseStream(stream);
    CFRelease(runLoopSource);
    CFRelease(eventTap);
    Pa_Terminate();

    return 0;
}
