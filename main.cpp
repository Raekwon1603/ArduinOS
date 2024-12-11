#include <Arduino.h>
#include <EEPROM.h>
#include "instruction_set.h"

// CLI
const int MAX_FILE_NAME_LENGTH = 12;
static char buffer[4][MAX_FILE_NAME_LENGTH];
static int bufferCounter = 0;
static int argumentCounter = 0;

// FAT
struct FATEntry {
    char name[12];
    int beginPosition;
    int length;
};

const int MAX_PROCESSES = 10;
int noOfFiles;
FATEntry FAT[MAX_PROCESSES];

// MEMORY
struct variable {
    byte name;
    int type;
    int length;
    int adress;
    int procID;
};
const int MAX_VARIABLES = 20;
const int MAXRAM = sizeof(variable) * MAX_VARIABLES;
int noOfVars = 0;
variable memoryTable[MAX_VARIABLES];
byte RAM[MAXRAM];

// PROCESS
struct process {
    char name[12];
    int procID;
    char state;
    int sp;
    int pc;
    int fp;
    int address;
};
const int PROCESS_TABLE_SIZE = 10;
int noOfProc;
int processCounter = 0;
process processTable[PROCESS_TABLE_SIZE];

// STACK
const int STACKSIZE = 16;
byte stack[PROCESS_TABLE_SIZE][STACKSIZE] = {0};

void store();
void retrieve();
void erase();
void files();
void freespace();
void run();
void list();
void suspend();
void resume();
void kill();

typedef struct {
    char name[MAX_FILE_NAME_LENGTH];
    void (*func)();
    int numberOfArguments;
} commandType;

static commandType commandList[] = {
    {"store", &store, 2}, {"retrieve", &retrieve, 1},   {"erase", &erase, 1},
    {"files", &files, 0}, {"freespace", &freespace, 0}, {"run", &run, 1},
    {"list", &list, 0},   {"suspend", &suspend, 1},     {"resume", &resume, 1},
    {"kill", &kill, 1},
};

/*  
 *  |-----------------------------------------------------------------------------------|
 *  |                               Command Line Interface                              |
 *  |-----------------------------------------------------------------------------------|
 */
// Check whether given function is known
bool checkCommand() {
    bool foundMatch = false;
    int commandLength = sizeof(commandList) / sizeof(commandType);
    // Loop through known commands
    for (int i = 0; i < commandLength; i++) {
        // Function is known
        if (strcmp(commandList[i].name, buffer[0]) == 0) {
            // Not enough arguments in call
            if (argumentCounter != commandList[i].numberOfArguments) {
                Serial.print(commandList[i].numberOfArguments);
                Serial.println(F(" arguments required"));
            } else {
                foundMatch = true;
                // Call function
                void (*func)() = commandList[i].func;
                func();
            }
        }
    }
    // Not a known command
    if (!foundMatch) {
          Serial.print("Command '");
          Serial.print(buffer[0]);
          Serial.println("' is not a known command.");
          Serial.println("Available commands:");
          for(auto command : commandList){
            Serial.println(command.name);
          }
    }
    return foundMatch;
}
// Function that updates input buffer
void inputCLI() {
    if (Serial.available() > 0) {
        delayMicroseconds(1042);
        int receivedChar = Serial.read();

        if (receivedChar == 32) {
            // Space pressed, go to next argument in buffer
            argumentCounter++;
            bufferCounter = 0;
        } else if (receivedChar == 13 || receivedChar == 10) {
            // Enter pressed, check command and clear buffer
            delayMicroseconds(1042);
            buffer[argumentCounter][bufferCounter] = '\0';
            Serial.read();
            checkCommand();
            for (int i = 0; i < 4; i++) {
                memset(buffer, 0, MAX_FILE_NAME_LENGTH);
            }
            bufferCounter = 0;
            argumentCounter = 0;
        } else {
            // Add char to buffer
            buffer[argumentCounter][bufferCounter] = receivedChar;
            bufferCounter++;
        }
    }
}
// Function validates input on numbers
bool isNumeric() {
    for (int i = 0; buffer[1][i] != '\0'; i++) {
        if (!isdigit(buffer[1][i])) {
            return false;
        }
    }
    return true;
}

/*  
 *  |-----------------------------------------------------------------------------------|
 *  |                                       FAT                                         |
 *  |-----------------------------------------------------------------------------------|
 */
// Function sets FAT entry on given index
void setFATEntry(int index, const FATEntry& entry) {
    int address = sizeof(noOfFiles) + (index * sizeof(FATEntry));
    EEPROM.put(address, entry);
}
// Function that returns FAT entry on index
FATEntry getFATEntry(int index) {
    FATEntry entry;
    int address = sizeof(noOfFiles) + (index * sizeof(FATEntry));
    EEPROM.get(address, entry);
    return entry;
}
// Write data in EEPROM
void writeFAT() {
    int address = 0;
    EEPROM.put(address, noOfFiles);
    address += sizeof(noOfFiles);
    for (int i = 0; i < noOfFiles; i++) {
        EEPROM.put(address, FAT[i]);
        address += sizeof(FATEntry);
    }
}
// Read FAT from EEPROM
void readFAT() {
    int address = 0;
    EEPROM.get(address, noOfFiles);
    address += sizeof(noOfFiles);
    for (int i = 0; i < MAX_PROCESSES; i++) {
        FAT[i] = getFATEntry(i);
    }
}
// Sort the FAT entries by position
void sortFAT() {
    bool sorted = false;
    while (!sorted) {
        sorted = true;
        for (int i = 0; i < noOfFiles - 1; i++) {
            if (FAT[i].beginPosition > FAT[i + 1].beginPosition) {
                // Swap the entries
                FATEntry temp = FAT[i];
                FAT[i] = FAT[i + 1];
                FAT[i + 1] = temp;
                sorted = false;
            }
        }
    }
}
// Function finds available position to store file
int findAvailablePosition(int fileSize) {
    sortFAT();
    // Check for space in the first block
    int systemMemory = (sizeof(FATEntry) * MAX_PROCESSES) + 1;

    int firstBlockEnd = FAT[0].beginPosition + FAT[0].length;
    if (firstBlockEnd - systemMemory >= fileSize) {
        return systemMemory;
    }
    // Check for space between blocks
    for (int i = 0; i < noOfFiles - 1; i++) {
        int currentBlockEnd = FAT[i].beginPosition + FAT[i].length;
        int nextBlockStart = FAT[i + 1].beginPosition;
        int availableSpace = nextBlockStart - currentBlockEnd;
        if (availableSpace >= fileSize) {
            return currentBlockEnd;
        }
    }
    // Check for space in last block
    int lastBlockEnd = FAT[noOfFiles - 1].beginPosition + FAT[noOfFiles - 1].length;
    int remainingSpace = EEPROM.length() - lastBlockEnd;
    if (remainingSpace >= fileSize) {
        if (noOfFiles == 0) {
            return lastBlockEnd + systemMemory;
        }
        return lastBlockEnd;
    }
    // No space available
    return -1;
}

// Function returns the index of the file in FAT
int getFileInFAT(const char* fileName) {
    // Load FAT from EEPROM
    readFAT();
    for (int i = 0; i < noOfFiles; i++) {
        if (strcmp(FAT[i].name, fileName) == 0) {
            return i;
        }
    }
    return -1;
}
// Function s file
void storeFile(const char* filename, int fileSize) {
    Serial.println(F("Give input for file:"));
    char fileData[fileSize];
    // Wait for data
    while (Serial.available() == 0) {
        
    }

    for (int i = 0; i < fileSize; i++) {
        if (Serial.available() != 0) {
            // Add data to filedata
            fileData[i] = Serial.read();
        } else {
            fileData[i] = 32;
        }
        delayMicroseconds(1042);
    }

    // Clear Serial buffer
    while (Serial.available()) {
        Serial.read();
        delayMicroseconds(1042);
    }
    // Get most recent FAT
    readFAT();

    if (noOfFiles >= MAX_PROCESSES) {
        Serial.println(F("File cannot be stored, limit reached."));
        return;
    }
    if (getFileInFAT(filename) != -1) {
        Serial.println(F("File cannot be stored, given name already exists."));
        return;
    }

    // Find available position to store the file
    int position = findAvailablePosition(fileSize);
    if (position == -1) {
        Serial.println(F("Error: No space left for file."));
        return;
    }

    // Make new FATEntry with new data
    FATEntry file = {};
    strcpy(file.name, filename);
    file.beginPosition = position;
    file.length = fileSize;

    // Write the FAT entry to the EEPROM
    FAT[noOfFiles] = file;
    noOfFiles++;
    sortFAT();
    writeFAT();
    // Write data to the EEPROM
    fileSize++;
    for (int i = 0; i < fileSize; i++) {
        EEPROM.write(position, fileData[i]);
        position++;
    }

    Serial.println(F("File has been stored."));
}

// Function to retrieve and print a file from the file system
void retrieveFile(const char* filename) {
    // Get most recent FAT
    readFAT();

    // Check if file exists
    int fatIndex = getFileInFAT(filename);
    if (fatIndex == -1) {
        Serial.println(F("File not found."));
        return;
    }

    int fileIndex = FAT[fatIndex].beginPosition;

    Serial.print(F("\nContent: "));
    for (int i = 0; i < FAT[fatIndex].length; i++) {
        Serial.print((char)EEPROM.read(fileIndex));
        fileIndex++;
    }
    Serial.print(F("\n"));
    Serial.println(F("End of File Content."));
}
// Function erases file
void eraseFile(const char* fileName) {
    // Get most recent FAT
    readFAT();
    int fatIndex = getFileInFAT(fileName);
    if (fatIndex == -1) {
        Serial.println(F("File not found."));
        return;
    }
    // Move other entries to the left
    for (int i = fatIndex; i < noOfFiles; i++) {
        FAT[i] = FAT[i + 1];
    }
    // Make last entry empty
    FATEntry emptyEntry;
    FAT[noOfFiles - 1] = emptyEntry;
    noOfFiles--;

    writeFAT();
    Serial.print(F("Erased: "));
    Serial.println(fileName);
}
// Function returns the available free space
void freespaceEEPROM() {
    // Get most recent FAT
    readFAT();

    int systemMemory = (sizeof(FATEntry) * MAX_PROCESSES) + 1;
    // Add total file sizes
    int usedSpace = 0;
    for (int i = 0; i < noOfFiles; i++) {
        usedSpace += FAT[i].length;
    }
    int totalAvailable = EEPROM.length() - systemMemory - usedSpace;
    Serial.print(F("Available space: "));
    Serial.println(totalAvailable);
}
// Print FAT
void printFAT() {
    readFAT();  // Read the FAT from EEPROM
    Serial.println();
    Serial.print(noOfFiles);
    Serial.println(F(" files found"));

    for (int i = 0; i < noOfFiles; i++) {
        Serial.print(F("File "));
        Serial.print(i);
        Serial.print(F(": Name="));
        Serial.print(FAT[i].name);
        Serial.print(F("     \tAddress = "));
        Serial.print(FAT[i].beginPosition);
        Serial.print(F("\tLength = "));
        Serial.println(FAT[i].length);
    }
    Serial.println();
}
// Clear EEPROM
void clearEeprom() {
    for (int i = 0; i < EEPROM.length(); i++) {
        EEPROM.write(i, 0);
    }
    Serial.println(F("\nEEPROM CLEARED\n"));
}

/*  
 *  |-----------------------------------------------------------------------------------|
 *  |                                       STACK                                       |
 *  |-----------------------------------------------------------------------------------|
 */
void pushByte(int procID, int& sp, byte b) { stack[procID][sp++] = b; }
byte popByte(int procID, int& sp) {
    return stack[procID][--sp];
}

void pushChar(int procID, int& sp, char c) {
    pushByte(procID, sp, c);
    // Push char
    pushByte(procID, sp, 0x01);
}
char popChar(int procID, int& sp) {
    return popByte(procID, sp);
}

void pushInt(int procID, int& sp, int i) {
    pushByte(procID, sp, highByte(i));
    pushByte(procID, sp, lowByte(i));
    // Push int
    pushByte(procID, sp, 0x02);
}
int popInt(int procID, int& sp) {
    byte lb = popByte(procID, sp);
    byte hb = popByte(procID, sp);
    int i = word(hb, lb);
    return i;
}

void pushFloat(int procID, int& sp, float f) {
    byte* b = (byte*)&f;
    for (int i = 3; i >= 0; i--) {
        // Push bytes beginning with highbytes
        pushByte(procID, sp, b[i]);
    }
    // Push float
    pushByte(procID, sp, 0x04);
}
float popFloat(int procID, int& sp) {
    byte b[4];
    for (int i = 0; i < 4; i++) {
        // Pop bytes beginning with lowbytes
        byte temp = popByte(procID, sp);
        b[i] = temp;
    }

    float* f = (float*)b;
    return *f;
}

void pushString(int procID, int& sp, char* s) {
    for (int i = 0; i < strlen(s); i++) {
        pushByte(procID, sp, s[i]);
    }
    // Push terminating zero
    pushByte(procID, sp, 0x00);
    // Push length
    pushByte(procID, sp, strlen(s) + 1);
    // Push string
    pushByte(procID, sp, 0x03);
}
char* popString(int procID, int& sp, int size) {
    char* temp = new char[size];
    // Pop string including terminating zero
    for (int i = size - 1; i >= 0; i--) {
        byte letter = popByte(procID, sp);
        temp[i] = letter;
    }
    return temp;
}

float popVal(int procID, int& sp, int type) {
    switch (type) {
        // Char
        case 1:
        {
            return popChar(procID, sp);
            break;
        }
        // Int
        case 2:
        {
            return popInt(procID, sp);
            break;
        }
        // Float
        case 4:
        {
            return popFloat(procID, sp);
            break;
        }
        default:
            break;
    }
}

/*  
 *  |-----------------------------------------------------------------------------------|
 *  |                                       MEMORY                                      |
 *  |-----------------------------------------------------------------------------------|
 */
// Save char to memory
void saveChar(char c, int adress) { RAM[adress] = c; }
// Load char from memory
char loadChar(int adress) { return RAM[adress]; }
// Save int to memory
void saveInt(int i, int adress) {
    RAM[adress] = highByte(i);
    RAM[adress + 1] = lowByte(i);
}
// Load int from memory
int loadInt(int adress) {
    byte hb = RAM[adress];
    byte lb = RAM[adress + 1];
    // Return merged
    return word(hb, lb);
}
// Save float to memory
void saveFloat(float f, int adress) {
    byte *b = (byte *)&f;
    // Push bytes starting with highbytes
    for (int i = 3; i >= 0; i--) {
        RAM[adress + i] = b[i];
    }
}
// Load float from memory
float loadFloat(int adress) {
    byte b[4];
    // Pop bytes starting with lowbytes
    for (int i = 0; i < 4; i++) {
        b[i] = RAM[adress + i];
    }

    float *f = (float *)b;
    return *f;
}
// Save string to memory
void saveString(char *s, int adress) {
    // Save chars
    for (int i = 0; i < strlen(s); i++) {
        RAM[adress + i] = s[i];
    }
    // Save terminating zero
    RAM[adress + strlen(s)] = 0x00;
}
// Load string from memory
char *loadString(int adress, int length) {
    char *temp = new char[length];
    // Pop chars including terminating zero
    for (int i = length - 1; i >= 0; i--) {
        temp[i] = RAM[adress + i];
    }
    return temp;
}
// Sort the memoryTable entries by position
void sortMemory() {
    bool sorted = false;
    while (!sorted) {
        sorted = true;
        for (int i = 0; i < noOfVars - 1; i++) {
            if (memoryTable[i].adress > memoryTable[i + 1].adress) {
                // Swap them
                variable temp = memoryTable[i];
                memoryTable[i] = memoryTable[i + 1];
                memoryTable[i + 1] = temp;
                sorted = false;
            }
        }
    }
}
// Function checks for available space in memory
int getAvailableSpace(int size) {
    // Check first block
    if (memoryTable[0].adress >= size) {
        return 0;
    }

    // Check between blocks
    for (int i = 0; i < noOfVars - 1; i++) {
        int availableSpace = memoryTable[i + 1].adress - (memoryTable[i].adress + memoryTable[i].length);
        if (availableSpace >= size) {
            return memoryTable[i].adress + memoryTable[i].length;
        }
    }

    // Check last block
    int lastEntry = memoryTable[noOfVars - 1].adress + memoryTable[noOfVars - 1].length;
    if (MAXRAM - (int)lastEntry >= size) {
        return lastEntry;
    }

    Serial.println("No space found");
    return -1;
}

int findFileInMemory(byte name, int procID) {
    for (int i = 0; i < noOfVars; i++) {
        if (memoryTable[i].name == name && memoryTable[i].procID == procID) {
            return i;
        }
    }
    return -1;  // Not found
}

void addMemoryEntry(byte name, int procID, int &stackP) {
    // Check if there is space in the memory table
    if (noOfVars >= MAX_VARIABLES) {
        Serial.print(F("Error. Not enough space in the memory table"));
        return;
    }

    // Check if variable is already in memorytable and should be overwritten
    int index = findFileInMemory(name, procID);

    if (index != -1) {
        // Shift variables to delete one
        for (int i = index; i < noOfVars; i++) {
            memoryTable[i] = memoryTable[i + 1];
        }
        noOfVars--;
    }
    // Index is after last var
    index = noOfVars;

    int type = popByte(procID, stackP);
    int size = (type != 3) ? type : popByte(procID, stackP);
    sortMemory();

    int newAdress = (noOfVars > 0) ? getAvailableSpace(size) : 0;
    variable newVariable = {.name = name,
                            .type = type,
                            .length = size,
                            .adress = newAdress,
                            .procID = procID};

    memoryTable[index] = newVariable;

    switch (type) {
        case 1: {
            // Char
            saveChar(popVal(procID, stackP, type), newAdress);
            break;
        }
        case 2: {
            // Int
            saveInt(popVal(procID, stackP, type), newAdress);
            break;
        }
        case 3: {
            // String
            char *s = popString(procID, stackP, size);
            saveString(s, newAdress);
            break;
        }
        case 4: {
            // Float
            saveFloat(popVal(procID, stackP, type), newAdress);
            break;
        }
        default:
            break;
    }

    noOfVars++;
}

void getMemoryEntry(byte name, int procID, int &stackP) {
    int index = findFileInMemory(name, procID);
    if (index == -1) {
        Serial.println("Error. This variable doesn't exist.");
        return;
    }

    int type = memoryTable[index].type;
    int length = memoryTable[index].length;
    switch (type) {
        case 1: {
            // Char
            char temp = loadChar(memoryTable[index].adress);
            pushChar(procID, stackP, temp);
            break;
        }
        case 2: {
            // Int
            int temp = loadInt(memoryTable[index].adress);
            pushInt(procID, stackP, temp);
            break;
        }
        case 3: {
            // String
            pushString(procID, stackP, loadString(memoryTable[index].adress, length));
            break;
        }
        case 4: {
            // Float
            pushFloat(procID, stackP, loadFloat(memoryTable[index].adress));
            break;
        }
        default:
            break;
    }
}

void deleteVars(int procID) {
    // Delete all variables for a process
    for (int j = 0; j < noOfVars; j++) {
        if (memoryTable[j].procID == procID) {
            for (int i = j; i < noOfVars; i++) {
                memoryTable[i] = memoryTable[i + 1];
            }
            noOfVars--;
        }
    }
}

/*  
 *  |-----------------------------------------------------------------------------------|
 *  |                                       PROCESS                                     |
 *  |-----------------------------------------------------------------------------------|
 */

int getPid(int id)
// Find the index of a process in the process
{
    for (int i = 0; i < noOfProc; i++) {
        if (processTable[i].procID == id) {
            // Return index
            return i;
        }
    }
    return -1;
}

void changeProcessState(int processIndex, char state) {
    // Change the state of a process in the process table
    if (state != 'r' && state != 'p' && state != '0') {
        Serial.println(F("Not a valid state"));
        return;
    }
    if (processTable[processIndex].state == state) {
        Serial.print(F("Process already is in "));
        Serial.print(state);
        Serial.println(F(" state"));
        return;
    }
    processTable[processIndex].state = state;
}

void runProcess(const char *filename) {
    // Run a new process

    // Check if process table has space
    if (noOfProc >= PROCESS_TABLE_SIZE) {
        Serial.println(F("Error. Not enough space in the process table"));
        return;
    }
    // Check if file exists
    int fileIndex = getFileInFAT(filename);
    
    if (fileIndex == -1) {
        Serial.println(F("File does not exist."));
        return;
    }

    // Initialize a new process with default values
    process newProcess;

    // Copy the filename to the process name
    strcpy(newProcess.name, filename);
    newProcess.procID = processCounter++;
    newProcess.state = 'r';
    newProcess.pc = 0;
    newProcess.fp = 0;
    newProcess.sp = 0;
    newProcess.address = FAT[fileIndex].beginPosition;

    processTable[noOfProc++] = newProcess;

    Serial.print(F("Proces: "));
    Serial.print(newProcess.procID);
    Serial.println(F(" has been started"));
}

// Suspend a process by changing its state to paused
void suspendProcess(int id) {
    Serial.print(F("Suspending process "));
    Serial.println(id);
    int processIndex = getPid(id);
    if (processIndex == -1) {
        Serial.println(F("processId doesn't exist"));
        return;
    }

    if (processTable[processIndex].state == '0') {
        Serial.println(F("Process already ended"));
        return;
    }

    changeProcessState(processIndex, 'p');
    Serial.print(F("Process with PID: "));
    Serial.print(id);
    Serial.println(F(" has been suspended."));
}

// Resume a suspended process by changing its state to running
void resumeProcess(int id) {
    int processIndex = getPid(id);
    if (processIndex == -1) {
        Serial.println(F("processId doesn't exist"));
        return;
    }

    if (processTable[processIndex].state == '0') {
        Serial.println(F("Process already ended"));
        return;
    }

    changeProcessState(processIndex, 'r');
    Serial.print(F("Process with PID: "));
    Serial.print(id);
    Serial.println(F(" has been resumed."));
}

// Stop a process by changing its state to terminated
void stopProcess(int id) {
    int processIndex = getPid(id);
    if (processIndex == -1) {
        Serial.println(F("processId doesn't exist"));
        return;
    }

    if (processTable[processIndex].state == '0') {
        Serial.println(F("Process already ended"));
        return;
    }
    // Delete all variables of process from memory
    deleteVars(id);
    changeProcessState(processIndex, '0'); // Change to terminated

    // Delete process from processTable
    for (int j = 0; j < noOfProc; j++) {
        if (processTable[j].procID == id) {
            for (int i = j; i < noOfProc; i++) {
                processTable[i] = processTable[i + 1];
            }
        }
    }
    Serial.print(F("Process with PID: "));
    Serial.print(id);
    Serial.println(F(" has been killed."));
    noOfProc--;
}

// Show the list of processes with their ID, state, and name
void showProcesses() {
    Serial.println(F("List of active processes:"));

    for (int i = 0; i < noOfProc; i++) {
        if (processTable[i].state != '0') {
            Serial.print(F("PID: "));
            Serial.print(processTable[i].procID);
            Serial.print(F(" - Status: "));
            Serial.print(processTable[i].state);
            Serial.print(F(" - Name: "));
            Serial.println(processTable[i].name);
        }
    }
}

float increment(int type, float value) { return value + 1; }
float decrement(int type, float value) { return value - 1; }

typedef struct {
    int operatorName;
    float (*func)(int type, float value);
    int returnType;
} unaryFunction;

unaryFunction unary[] = {
    {INCREMENT, &increment, 0},
    {DECREMENT, &decrement, 0},
};

float plus(float x, float y) { return x + y; }
float minus(float x, float y) { return x - y; }

typedef struct {
    int operatorName;
    float (*func)(float x, float y);
    int returnType;
} binaryFunction;

binaryFunction binary[] = {
    {PLUS, &plus, 0},
    {MINUS, &minus, 0}
};

// Function to find the index of a unary function in the unary array
int findUnaryFunction(int operatorNum) {
    for (int i = 0; i < 2; i++) {
        if (unary[i].operatorName == operatorNum) {
            return i;
        }
    }
    return -1;
}

// Function to find the index of a binary function in the binary array
int findBinaryFunction(int operatorNum) {
    for (int i = 0; i < 2; i++) {
        if (binary[i].operatorName == operatorNum) {
            return i;
        }
    }
    return -1;
}

// Function to execute a process at a given index in the processTable
void execute(int index) {
    int address = processTable[index].address;
    int procID = processTable[index].procID;
    int& stackP = processTable[index].sp;
    byte currentCommand = EEPROM.read(address + processTable[index].pc);
    processTable[index].pc++;
    switch (currentCommand) {
        case CHAR: {
            // Handle CHAR bytecode
            char temp = (char)EEPROM.read(address + processTable[index].pc++);
            pushChar(procID, stackP, temp);
            break;
        }
        case INT: {
            // Handle INT bytecode
            int highByte = EEPROM.read(address + processTable[index].pc++);
            int lowByte = EEPROM.read(address + processTable[index].pc++);
            pushInt(procID, stackP, word(highByte, lowByte));
            break;
        }
        case STRING: {
            // Handle STRING bytecode
            char string[12];
            memset(&string[0], 0, sizeof(string));  // Empty string
            int pointer = 0;
            do {
                int temp = (int)EEPROM.read(address + processTable[index].pc++);
                string[pointer] = (char)temp;
                pointer++;
            } while (string[pointer - 1] != 0);

            pushString(procID, stackP, string);
            break;
        }
        case FLOAT: {
            // Handle FLOAT bytecode
            byte b[4];
            for (int i = 3; i >= 0; i--) {
                byte temp = EEPROM.read(address + processTable[index].pc++);
                b[i] = temp;
            }
            float* f = (float*)b;
            pushFloat(procID, stackP, *f);
            break;
        }
        case STOP: {
            // Handle STOP bytecode
            Serial.print(F("Process with pid: "));
            Serial.print(procID);
            Serial.println(F(" is finished."));
            deleteVars(procID);
            stopProcess(procID);
            Serial.println();
            break;
        }
        case 51 ... 52: {  // PRINT and PRINTLN
            // Handle PRINT and PRINTLN commands
            int type = popByte(procID, stackP);
            switch (type) {
                case CHAR: {
                    Serial.print(popChar(procID, stackP));
                    break;
                }
                case INT: {
                    Serial.print(popInt(procID, stackP));
                    break;
                }
                case STRING: {
                    int size = popByte(procID, stackP);
                    Serial.print(popString(procID, stackP, size));
                    break;
                }
                case FLOAT: {
                    Serial.print(popFloat(procID, stackP), 5);
                    break;
                }
                default:
                    break;
            }
            if (currentCommand == 52) {
                Serial.println();
            }
            break;
        }
        case SET: {
            // Handle SET bytecode
            char name = EEPROM.read(address + processTable[index].pc++);

            addMemoryEntry(name, procID, stackP);
            break;
        }
        case GET: {
            // Handle GET bytecode
            char name = EEPROM.read(address + processTable[index].pc++);
            getMemoryEntry(name, procID, stackP);
            break;
        }
        case DELAY: {
            break;
        }
        case DELAYUNTIL: {
            popByte(procID, stackP);
            int temp = popInt(procID, stackP);
            int mil = millis();
            if (temp > mil) {
                processTable[index].pc--;
                pushInt(procID, stackP, temp);
            }
            break;
        }
        case MILLIS: {
            pushInt(procID, stackP, millis());
            break;
        }
        case PINMODE: {
            popByte(procID, stackP);
            int direction = popInt(procID, stackP);
            popByte(procID, stackP);
            int pin = popInt(procID, stackP);
            pinMode(pin, direction);
            break;
        }
        case DIGITALWRITE: {
            popByte(procID, stackP);
            int status = popInt(procID, stackP);
            popByte(procID, stackP);
            int pin = popInt(procID, stackP);
            digitalWrite(pin, status);
            break;
        }
        case FORK: {
            int type = popByte(procID, stackP);
            int size = popByte(procID, stackP);
            char* fileName = popString(procID, stackP, size);
            runProcess(fileName);
            pushInt(procID,stackP,procID+1);
            break;
        }
        case WAITUNTILDONE: {
            popByte(procID,stackP);
            int runningID = popInt(procID, stackP);
            char state = processTable[runningID].state;
            if(state == 'r' || state == 'p') {
                processTable[procID].pc--;
                pushInt(procID, stackP, runningID);
            } 
            break;
        }
        case 7 ... 8: {
            // Handle unary functions
            int type = popByte(procID, stackP);
            float value = popVal(procID, stackP, type);

            float newValue = unary[findUnaryFunction(currentCommand)].func(type, value);

            switch (type) {
                case CHAR: {
                    pushChar(procID, stackP, (char)newValue);
                    break;
                }
                case INT: {
                    pushInt(procID, stackP, (int)newValue);
                    break;
                }
                case FLOAT: {
                    pushFloat(procID, stackP, (float)newValue);
                    break;
                }
                default:
                    Serial.println(F("Execute: Default case"));
                    break;
            }
            break;
        }
        case 9 ... 10: {
            int typeY = popByte(procID, stackP);
            float y = popVal(procID, stackP, typeY);
            int typeX = popByte(procID, stackP);
            float x = popVal(procID, stackP, typeY);

            float newValue = binary[findBinaryFunction(currentCommand)].func(x, y);
            int returnType = max(typeY, typeX);
            switch (returnType) {
                case CHAR: {
                    pushChar(procID, stackP, (char)newValue);
                    break;
                }
                case INT: {
                    pushInt(procID, stackP, (int)newValue);
                    break;
                }
                case FLOAT: {
                    pushFloat(procID, stackP, (float)newValue);
                    break;
                }
                default:
                    Serial.println(F("Execute: Default case"));
                    break;
            }
            break;
        }
        default: {
            Serial.println(F("Error. Unkown commandList."));
            break;
        }
    }
}

void runProcesses() {
    for (int i = 0; i < noOfProc; i++) {
        if (processTable[i].state == 'r') {
            execute(i);
        }
    }
}

void setup() {
    Serial.begin(9600);
    Serial.println(F("\nArduinOS 1.0 ready.\n"));
}

void loop() {
    inputCLI();
    runProcesses();
}

void store() {
    // Store a file in the file system
    storeFile(buffer[1], atoi(buffer[2]));
}
void retrieve() {
    // Retrieve a file from the file system
    retrieveFile(buffer[1]);
}
void erase() {
    // Delete a file
    eraseFile(buffer[1]);
}
void files() {
    // Print a list of stored files
    printFAT();
}
void freespace() {
    // Print the available space in file system
    freespaceEEPROM();
}
void run() {
    // Start a program
    runProcess(buffer[1]);
}
void list() {
    //show active status of running programs
    showProcesses();
}
void suspend() {
    // Suspend a process
    if (isNumeric()) {
        suspendProcess(atoi(buffer[1]));
    } else {
        Serial.println(F("Error. Invalid process ID."));
    }
}
void resume() {
    // Resume a process
    if (isNumeric()) {
        resumeProcess(atoi(buffer[1]));
    } else {
        Serial.println(F("Error. Invalid process ID."));
    }
}
void kill() {
    // Stop a process
    if (isNumeric()) {
        stopProcess(atoi(buffer[1]));
    } else {
        Serial.println(F("Error. Invalid process ID."));
    }
}