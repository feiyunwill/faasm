#include <faasm/faasm.h>
#include <malloc.h>
#include <stdio.h>
#include <string.h>

int main(int argc, char* argv[])
{
    long inputSize = faasmGetInputSize();

    // Handle empty input
    if (inputSize == 0) {
        printf("Nothing to echo\n");
        const char* output = "Nothing to echo";
        uint8_t* bytesOutput = (uint8_t*)output;

        faasmSetOutput(bytesOutput, strlen(output));
        return 0;
    }

    uint8_t* inputBuffer = malloc(inputSize * sizeof(uint8_t));
    faasmGetInput(inputBuffer, inputSize);

    char* inputStr = (char*)inputBuffer;
    printf("Echoing %s\n", inputStr);

    faasmSetOutput(inputBuffer, inputSize);

    free(inputBuffer);

    return 0;
}
