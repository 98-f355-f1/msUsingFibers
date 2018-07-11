/*
USING FIBERS
The CreateFiber function create a new fiber for a thread. The creating thread must
specify the starting address of the code that the new fiber is to execute.
Typically, the starting address is the name of a user-suppled function. Multiple
fibers can execute the same function.

The following example demonstrates how to create, schedule, and delete fibers.
The fibers execute the locally defined functions ReadFiberFunc and WriteFiberFunc.
This example implements a fiber-based file copy operation. When running the example,
you must specify the source and destination files. Note that there are many other
ways to copy file programmatically, this example exists primarily to illustrate
the use of the fiber functions.
*/

#include <windows.h>
#include <tchar.h>
#include <stdio.h>

VOID
__stdcall
ReadFiberFunc(LPVOID lpParameter);

VOID
__stdcall
WriteFiberFunc(LPVOID lpParameter);

void DisplayFiberInfo(void);

typedef struct
{
	DWORD dwParameter;				// DWORD parameter to fiber (unused)
	DWORD dwFiberResultCode;		// GetLastError() result code
	HANDLE hFile;					// handle to operate on
	DWORD dwBytesProcessed;			// number of bytes processed
} FIBERDATASTRUCT, *PFIBERDATASTRUCT, *LPFIBERDATASTRUCT;


#define RTN_OK 0
#define RTN_USAGE 1
#define RTN_ERROR 13

#define BUFFER_SIZE 32768			// read/write buffer size
#define FIBER_COUNT 3				// max fibers (including primary)

#define PRIMARY_FIBER 0				// array index to primary fiber
#define READ_FIBER 1				// array index to read fiber
#define WRITE_FIBER 2				// array index to write fiber

LPVOID g_lpFiber[FIBER_COUNT];
LPVOID g_lpBuffer;

DWORD g_dwBytesRead;

int __cdecl _tmain(int argc, TCHAR *argv[])
{
	LPFIBERDATASTRUCT fs;

	if (argc != 3)
	{
		printf("Usage: %s <SourceFile> <DestinationFile>\n", argv[0]);
		return RTN_USAGE;
	}

	// allocate storage for our fiber data structures
	fs = (LPFIBERDATASTRUCT)HeapAlloc(
		GetProcessHeap(), 0,
		sizeof(FIBERDATASTRUCT) * FIBER_COUNT);

	if (fs == NULL)
	{
		printf("HeapAlloc error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	// allocate storage for the read/write buffer
	g_lpBuffer = (LPBYTE)HeapAlloc(GetProcessHeap(), 0, BUFFER_SIZE);
	if (g_lpBuffer == NULL)
	{
		printf("HeapAlloc error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	// open the source file
	fs[READ_FIBER].hFile = CreateFile(
		argv[1],
		GENERIC_READ,
		FILE_SHARE_READ,
		NULL,
		OPEN_EXISTING,
		FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);

	if (fs[READ_FIBER].hFile == INVALID_HANDLE_VALUE)
	{
		printf("Create READ_FIBER File error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	// open the destination file
	fs[WRITE_FIBER].hFile = CreateFile(
		argv[2],
		GENERIC_WRITE,
		0,
		NULL,
		CREATE_NEW,
		FILE_FLAG_SEQUENTIAL_SCAN,
		NULL);

	if (fs[WRITE_FIBER].hFile == INVALID_HANDLE_VALUE)
	{
		printf("Create WRITE_FIBER File error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	// convert thread to a fiber, to allow scheduling other fibers
	g_lpFiber[PRIMARY_FIBER] = ConvertThreadToFiber(&fs[PRIMARY_FIBER]);
	if (g_lpFiber[PRIMARY_FIBER] == NULL)
	{
		printf("ConvertThreadToFiber error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	// initialize the primary fiber data structure. we don't use
	// the primary fiber data structure for anything in this sample
	fs[PRIMARY_FIBER].dwParameter = 0;
	fs[PRIMARY_FIBER].dwFiberResultCode = 0;
	fs[PRIMARY_FIBER].hFile = INVALID_HANDLE_VALUE;

	// create the read fiber
	g_lpFiber[READ_FIBER] = CreateFiber(0, ReadFiberFunc, &fs[READ_FIBER]);
	if (g_lpFiber[READ_FIBER] == NULL)
	{
		printf("CreateFiber error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	fs[READ_FIBER].dwParameter = 0x12345678;

	// create the write fiber
	g_lpFiber[WRITE_FIBER] = CreateFiber(0, WriteFiberFunc, &fs[WRITE_FIBER]);
	if (g_lpFiber[WRITE_FIBER] == NULL)
	{
		printf("CreateFiber error (%d)\n", GetLastError());
		return RTN_ERROR;
	}

	fs[WRITE_FIBER].dwParameter = 0x54545454;

	// switch to the read fiber
	SwitchToFiber(g_lpFiber[READ_FIBER]);

	// we have be scheduled again. Display results from
	// the read/write fiber
	printf("ReadFiber: result code is %lu, %lu bytes processed\n",
		fs[READ_FIBER].dwFiberResultCode, fs[READ_FIBER].dwBytesProcessed);

	printf("WriteFiber: result code is %lu, %lu bytes processed\n",
		fs[WRITE_FIBER].dwFiberResultCode, fs[WRITE_FIBER].dwBytesProcessed);

	// delete the fibers
	DeleteFiber(g_lpFiber[READ_FIBER]);
	DeleteFiber(g_lpFiber[WRITE_FIBER]);

	// close the fibers
	CloseHandle(fs[READ_FIBER].hFile);
	CloseHandle(fs[WRITE_FIBER].hFile);

	// free allocated memory
	HeapFree(GetProcessHeap(), 0, g_lpBuffer);
	HeapFree(GetProcessHeap(), 0, fs);

	return RTN_OK;
}

VOID
__stdcall
ReadFiberFunc(LPVOID lpParameter)
{
	LPFIBERDATASTRUCT fds = (LPFIBERDATASTRUCT)lpParameter;

	// if this fiber passed NULL for fiber data, just return,
	// causing the current thread to exit
	if (fds == NULL)
	{
		printf("Passed NULL fiber data; exiting the current thread.\n");
		return;
	}

	// display some information pertaining to the current fiber
	DisplayFiberInfo();

	fds->dwBytesProcessed = 0;

	while (1)
	{
		// read data from file specified in the READ_FIBER structure
		if (!ReadFile(fds->hFile, g_lpBuffer, BUFFER_SIZE, &g_dwBytesRead, NULL))
		{
			break;
		}

		// if we reached EOF, break
		if (g_dwBytesRead == 0) break;

		// update number of bytes processed in the fiber data structure
		fds->dwBytesProcessed += g_dwBytesRead;

		// switch to the write fiber
		SwitchToFiber(g_lpFiber[WRITE_FIBER]);
	} // end while loop


	  // update the fiber result code
	fds->dwFiberResultCode = GetLastError();

	// switch back to the primary fiber
	SwitchToFiber(g_lpFiber[PRIMARY_FIBER]);
}

VOID
__stdcall
WriteFiberFunc(LPVOID lpParameter)
{
	LPFIBERDATASTRUCT fds = (LPFIBERDATASTRUCT)lpParameter;
	DWORD dwBytesWritten;

	// if this fiber was passed NULL for fiber data, just return,
	// cause the current thread to exit
	if (fds == NULL)
	{
		printf("Passed NULL fiber data; exiting current thread.\n");
		return;
	}

	DisplayFiberInfo();

	// assume all writes suceeded. if a write fails, the fiber
	// result code will be updated to reflect the reason for failure
	fds->dwBytesProcessed = 0;
	fds->dwFiberResultCode = ERROR_SUCCESS;

	while (1)
	{
		// write data to the file specified in the WRITE_FIBER structure
		if (!WriteFile(fds->hFile, g_lpBuffer, g_dwBytesRead,
			&dwBytesWritten, NULL))
		{
			// if an error occurred writing, break;
			break;
		}

		// update number of bytes processed in the fiber data structure
		fds->dwBytesProcessed += dwBytesWritten;

		// switch back to the fiber
		SwitchToFiber(g_lpFiber[READ_FIBER]);
	} // end while

	  // if an error occurred, update the fiber result code
	fds->dwFiberResultCode = GetLastError();

	// switch to the primary fiber
	SwitchToFiber(g_lpFiber[PRIMARY_FIBER]);
}

void DisplayFiberInfo(void)
{
	LPFIBERDATASTRUCT fds = (LPFIBERDATASTRUCT)GetFiberData();
	LPVOID lpCurrentFiber = GetCurrentFiber();

	// determin which fiber is executing, based on the fiber address
	if (lpCurrentFiber == g_lpFiber[READ_FIBER])
		printf("Read fiber entered");
	else
	{
		if (lpCurrentFiber == g_lpFiber[WRITE_FIBER])
			printf("Primary fiber entered");
		else
			printf("Unknown fiber entered");
	}

	// display dwParameter from the current fiber data structure
	printf(" (dwParameter is 0x%lx)\n", fds->dwParameter);
}

