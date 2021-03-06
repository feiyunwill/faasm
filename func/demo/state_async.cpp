#include <faasm/faasm.h>

int main(int argc, char* argv[])
{
    const char* key = "state_async_example";

    // Read and write async
    uint8_t value[4] = { 3, 2, 1, 0 };
    faasmWriteState(key, value, 4);

    uint8_t readValueAsync[4];
    faasmReadState(key, readValueAsync, 4);

    // Push and read again
    faasmPushState(key);
    uint8_t readValueSync[4];
    faasmReadState(key, readValueSync, 4);

    // Check things are equal
    uint8_t equal[4];
    for (int i = 0; i < 4; i++) {
        bool isEqual = readValueAsync[i] == readValueSync[i];
        equal[i] = uint8_t(isEqual);
    }

    faasmSetOutput(equal, 4);

    return 0;
}
