
//#include "../SerialAsyncMFCTest/stdafx.h"

#include "stdafx.h"

#include "../CUL_Monitor/Win32_Serial.h"
/*************************************************************************************************/
CWin32_Serial::CWin32_Serial()
{
	m_hSerPort		= INVALID_HANDLE_VALUE;						// Handle setzen
	m_hReadEvent	= NULL;										// Eventobjekt, wartet auf ein Zeichen
	m_hWriteEvent	= NULL;										// Eventobjekt, wartet auf ein Zeichen
	memset(&m_Dcb, 0, sizeof(DCB));								// device control block löschen
	m_Dcb.DCBlength	= sizeof(DCB);								// The length of the structure, in bytes. The caller must set this member to sizeof(DCB). 
	memset(&m_Timeout, -1, sizeof(COMMTIMEOUTS));
	m_lastError		= ERROR_SUCCESS;
	m_bAsync		= false;									// Asynchron Modus?
}
/*************************************************************************************************/
CWin32_Serial::~CWin32_Serial(void)
{
	close();
}
/*************************************************************************************************/
//
//	Noch ein Test durchführen mit DCB.EofChar Das könnte die CWin32_Serial::readline() Methode massiv vereinfachen.
//
//	Aus: https://docs.microsoft.com/en-us/windows/desktop/cimwin32prov/win32-serialportconfiguration
//
//	EOFCharacter
//		Data type: uint32
//		Access type: Read-only
//		Qualifiers: MappingStrings ("Win32API|Communication Structures|DCB|EofChar")
//		Value of the character used to signal the end of data.
//		Example: ^Z
//
//	Siehe auch: https://docs.microsoft.com/en-us/windows/desktop/api/fileapi/nf-fileapi-readfile
//
//
//	A B E R   A C H T U N G ! !
//
//	Aus: https://stackoverflow.com/questions/23082515/readfile-eof-char
//	Frage:		I'm using serial com port communication synchronously with WinApi. And I want to split messages with 13 (CR). 
//				How can I use ReadFile function so that it returns when it reads a 13 ? 
//
//	Antwort:	No, that's very unlikely to work. Windows itself doesn't do anything with the DCB.EofChar you specify. Or for that matter 
//				any of the other special characters in the DCB. It directly passes it to the device driver, the underlying ioctl is IOCTL_SERIAL_SET_CHARS. 
//				Leaving it entirely up to the driver to implement them.
//
//				Most hardware vendors that write serial port drivers (USB emulators most of all these days), use the sample driver code that's included in the WDK. 
//				Which does nothing with SERIAL_CHARS.EofChar. So the inevitable outcome is that nobody implements it. I personally have never encountered one, 
//				knowingly anyway.
//
//				So it is expected not to have any effect.
//
//				You can usually expect DCB.EvtChar to work, it powers the EV_RXFLAG option for WaitCommEvent(). In other words, if you set it to the line 
//				terminator then it can give you a signal that ReadFile() is going to return at least a complete line.
//
//				But you explicitly said you don't want to do this. Everybody solves this by simply buffering the extra data that is returned by ReadFile(). 
//				Or by reading one byte at a time, which is okay because serial ports are slow anyway.
//
//	Vielleicht nochmal folgendes testen:
//	https://docs.microsoft.com/en-us/windows-hardware/drivers/ddi/content/ntddser/ni-ntddser-ioctl_serial_set_chars
//
bool CWin32_Serial::open(std::string deviceName, bool bAsync)
{
	m_bAsync	= bAsync;
	m_portName	= deviceName;
	m_hSerPort	= INVALID_HANDLE_VALUE;

	DWORD	dwFlagsAndAttributes = FILE_ATTRIBUTE_NORMAL;		// The file does not have other attributes set. This attribute is valid only if used alone.
	if (bAsync)
	{
		dwFlagsAndAttributes = 
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED;		// FILE_FLAG_OVERLAPPED = The file or device is being opened or created for asynchronous I/O.
//	HANDLE WINAPI CreateEvent(												
//		__in_opt  LPSECURITY_ATTRIBUTES lpEventAttributes,		A pointer to a SECURITY_ATTRIBUTES structure. If this parameter is NULL, 
//																the handle cannot be inherited by child processes. 
//		__in      BOOL bManualReset,							If this parameter is TRUE, the function creates a manual-reset event object, 
//																which requires the use of the ResetEvent function to set the event state to nonsignaled. 
//																If this parameter is FALSE, the function creates an auto-reset event object, and system 
//																automatically resets the event state to nonsignaled after a single waiting thread has been released. 
//		__in      BOOL bInitialState,							If this parameter is TRUE, the initial state of the event object is signaled; otherwise, it is nonsignaled. 
//		__in_opt  LPCTSTR lpName);								The name of the event object. The name is limited to MAX_PATH characters. Name comparison is case sensitive. 
//																If lpName matches the name of an existing named event object, this function requests the 
//																EVENT_ALL_ACCESS access right. In this case, the bManualReset and bInitialState parameters are ignored 
//																because they have already been set by the creating process. If the lpEventAttributes parameter is not NULL, 
//																it determines whether the handle can be inherited, but its security-descriptor member is ignored. 
//																If lpName is NULL, the event object is created without a name. 
		m_hReadEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	// Das Event-Objekt erzeugen
		if (NULL == m_hReadEvent)
		{
			#ifdef __AFX_H__
			TRACE("CWin32_Serial::open(): Read-Event-Objekt kann nicht erzeugt werden.");
			#endif
			return false;
		}
		m_hWriteEvent = CreateEvent(NULL, FALSE, FALSE, NULL);	// Das Event-Objekt erzeugen
		if (NULL == m_hWriteEvent)
		{
			#ifdef __AFX_H__
			TRACE("CWin32_Serial::open(): Write-Event-Objekt kann nicht erzeugt werden.");
			#endif
			return false;
		}
	}
// Com-Port öffnen mit CreateFile()
//	HANDLE WINAPI CreateFile(									Creates or opens a file or I/O device. The most commonly used I/O devices are as follows: 
//																file, file stream, directory, physical disk, volume, console buffer, tape drive, communications resource, 
//																mailslot, and pipe. The function returns a handle that can be used to access the file or device for various 
//																types of I/O depending on the file or device and the flags and attributes specified.
//		__in		LPCTSTR lpFileName,							The name of the file or device to be created or opened.
//		__in		DWORD dwDesiredAccess,						The requested access to the file or device, which can be summarized as read, write, both or neither (zero). 
//		__in		DWORD dwShareMode,							The requested sharing mode of the file or device, which can be read, write, both, delete, all of these, 
//																or none. Access requests to attributes or extended attributes are not affected by this flag.
//		__in_opt	LPSECURITY_ATTRIBUTES lpSecurityAttributes,	A pointer to a SECURITY_ATTRIBUTES structure that contains two separate but related data members: 
//																an optional security descriptor, and a Boolean value that determines whether the returned 
//																handle can be inherited by child processes. 
//		__in		DWORD dwCreationDisposition,				An action to take on a file or device that exists or does not exist.
//		__in		DWORD dwFlagsAndAttributes,					The file or device attributes and flags, FILE_ATTRIBUTE_NORMAL being the most common default value for files.
//		__in_opt	HANDLE hTemplateFile);						A valid handle to a template file with the GENERIC_READ access right. The template file supplies 
//																file attributes and extended attributes for the file that is being created.
//
//	Communications Resources 
//	========================
//	The CreateFile function can create a handle to a communications resource, such as the serial port COM1. For communications resources, the 
//	dwCreationDisposition parameter must be OPEN_EXISTING, the dwShareMode parameter must be zero (exclusive access), and the hTemplateFile parameter must be NULL. 
//	Read, write, or read/write access can be specified, and the handle can be opened for overlapped I/O. 
//	To specify a COM port number greater than 9, use the following syntax: "\\.\COM10". This syntax works for all port numbers and hardware that allows COM port numbers to be specified.
//
//	For more information about communications, see Communications. 

	m_hSerPort = CreateFile(("\\\\.\\"+deviceName).c_str(),		// Dem Filenamen muss "\\.\" vorangestellt werden. (Bsp.:"\\.\COM2")
							GENERIC_READ | GENERIC_WRITE,		// Schreib-/Lesezugriff gewünscht
							0,									// The dwShareMode parameter must be zero (exclusive access)
							NULL,								// Handle kann an alle "child processes" vererbt werden
							OPEN_EXISTING,						// The dwCreationDisposition parameter must be OPEN_EXISTING.
							dwFlagsAndAttributes,				// siehe oben.
							NULL);								// The hTemplateFile parameter must be NULL.
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		#ifdef __AFX_H__
		TRACE("CWin32_Serial::open(): COM-Port kann nicht geöffnet werden.");
		#endif
		m_lastError = GetLastError();
		return false;
	}

//	BOOL WINAPI GetCommState(									Retrieves the current control settings for a specified communications device.
//		__in		HANDLE hFile,								A handle to the communications device. The CreateFile function returns this handle. 
//		__inout		LPDCB lpDCB);								A pointer to a DCB structure that receives the control settings information. 
//	Return Value:												If the function succeeds, the return value is nonzero.
	if (FALSE == GetCommState(m_hSerPort, &m_Dcb))
	{
		m_lastError = GetLastError();
		return false;
	}

//	BOOL WINAPI GetCommTimeouts(								Retrieves the time-out parameters for all read and write operations on a specified communications device.		
//		__in		HANDLE hFile,								A handle to the communications device. The CreateFile function returns this handle. 
//		__out		LPCOMMTIMEOUTS lpCommTimeouts);				A pointer to a COMMTIMEOUTS structure in which the time-out information is returned. 
//	
//	Return Value:												If the function succeeds, the return value is nonzero.
//																If the function fails, the return value is zero. To get extended error information, call GetLastError. 
	if (FALSE == GetCommTimeouts(m_hSerPort, &m_Timeout))
	{
		m_lastError = GetLastError();
		return false;
	}
	
	return true;
}
/*************************************************************************************************/
bool CWin32_Serial::close()
{
	if (NULL != m_hReadEvent)
	{
		CloseHandle(m_hReadEvent);
		m_hReadEvent = NULL;
	}
	if (NULL != m_hWriteEvent)
	{
		CloseHandle(m_hWriteEvent);
		m_hWriteEvent = NULL;
	}
	if (INVALID_HANDLE_VALUE == m_hSerPort)
		return true;

	BOOL retValue;
//	BOOL WINAPI CancelIoEx(
//		__in		HANDLE hFile,								A handle to the file
//		__in_opt	LPOVERLAPPED lpOverlapped);					A pointer to an OVERLAPPED data structure that contains the data used for asynchronous I/O. 
//																If this parameter is NULL, all I/O requests for the hFile parameter are canceled. 
//																If this parameter is not NULL, only those specific I/O requests that were issued for the file with 
//																the specified lpOverlapped overlapped structure are marked as canceled, meaning that you can cancel 
//																one or more requests, while the CancelIo function cancels all outstanding requests on a file handle. 
// 
//	Return Value:												If the function succeeds, the return value is nonzero. The cancel operation for all pending I/O operations 
//																issued by the calling thread for the specified file handle was successfully requested. The application must 
//																not free or reuse the OVERLAPPED structure associated with the canceled I/O operations until they have completed. 
//																The thread can use the GetOverlappedResult function to determine when the I/O operations themselves have been completed. 
//																If the function fails, the return value is 0 (zero). To get extended error information, call the GetLastError function. 
//																If this function cannot find a request to cancel, the return value is 0 (zero), and GetLastError returns ERROR_NOT_FOUND. 
//	retValue = CancelIoEx(m_hSerPort, NULL);	Diese Funktion wird unter Windows XP noch nicht unterstützt.

//	BOOL WINAPI CancelIo(										Cancels all pending input and output (I/O) operations that are issued by the calling thread for the specified file. 
//																The function does not cancel I/O operations that other threads issue for a file handle.
//																To cancel I/O operations from another thread, use the CancelIoEx function. 
//		__in  HANDLE hFile);									A handle to the file. The function cancels all pending I/O operations for this file handle.
	retValue = CancelIo(m_hSerPort);
	if (FALSE == retValue)
	{
		m_lastError = GetLastError();
		if(ERROR_NOT_FOUND != m_lastError)
			return false;
	}

//	BOOL WINAPI CloseHandle(									Closes an open object handle.
//		__in		HANDLE hObject);							A valid handle to an open object.
//	Return Value:												If the function succeeds, the return value is nonzero.
	if (false == CloseHandle(m_hSerPort))
	{
		m_lastError = GetLastError();
		return false;
	}

	m_hSerPort = INVALID_HANDLE_VALUE;
	return true;
}
/*************************************************************************************************/
bool CWin32_Serial::isOpen()
{
	return  !(INVALID_HANDLE_VALUE == m_hSerPort);
}
/*************************************************************************************************/
bool CWin32_Serial::isAsynchron()
{
	return m_bAsync;
}
/*************************************************************************************************/
void CWin32_Serial::printConfiguration(std::ostream &DebugOut) const
{
	DebugOut << "Port Name:     " << m_portName		<< std::endl;
	DebugOut << "Handle:        " << m_hSerPort		<< std::endl;
	DebugOut << "Last Error:    " << m_lastError	<< std::endl;
	if (m_bAsync)
		DebugOut << "Asynchroner Modus" << std::endl;
	else
		DebugOut << "Synchroner Modus" << std::endl;

// Ausgabe der Konfiguration:
//	typedef struct _DCB {
//		DWORD DCBlength;				The length of the structure, in bytes. The caller must set this member to sizeof(DCB). 
//		DWORD BaudRate;					The baud rate at which the communications device operates. This member can be an actual baud rate value, or one of the following indexes.
//		DWORD fBinary  :1;				If this member is TRUE, binary mode is enabled. Windows does not support nonbinary mode transfers, so this member must be TRUE.
//		DWORD fParity  :1;				If this member is TRUE, parity checking is performed and errors are reported.
//		DWORD fOutxCtsFlow  :1;			If this member is TRUE, the CTS (clear-to-send) signal is monitored for output flow control. If this member is TRUE and CTS is turned off, 
//											output is suspended until CTS is sent again.
//		DWORD fOutxDsrFlow  :1;			If this member is TRUE, the DSR (data-set-ready) signal is monitored for output flow control. If this member is TRUE and DSR is turned off, 
//											output is suspended until DSR is sent again.
//		DWORD fDtrControl  :2;			The DTR (data-terminal-ready) flow control. This member can be one of the following values. ...
//		DWORD fDsrSensitivity  :1;		If this member is TRUE, the communications driver is sensitive to the state of the DSR signal. The driver ignores any bytes received, 
//											unless the DSR modem input line is high.
//		DWORD fTXContinueOnXoff  :1;	If this member is TRUE, transmission continues after the input buffer has come within XoffLim bytes of being full and the driver has transmitted 
//											the XoffChar character to stop receiving bytes. If this member is FALSE, transmission does not continue until the input buffer is within 
//											XonLim bytes of being empty and the driver has transmitted the XonChar character to resume reception. 
//		DWORD fOutX  :1;				Indicates whether XON/XOFF flow control is used during transmission. If this member is TRUE, transmission stops when the XoffChar character 
//											is received and starts again when the XonChar character is received. 
//		DWORD fInX  :1;					Indicates whether XON/XOFF flow control is used during reception. If this member is TRUE, the XoffChar character is sent when the input buffer 
//											comes within XoffLim bytes of being full, and the XonChar character is sent when the input buffer comes within XonLim bytes of being empty. 
//		DWORD fErrorChar  :1;			Indicates whether bytes received with parity errors are replaced with the character specified by the ErrorChar member. If this member is TRUE 
//											and the fParity member is TRUE, replacement occurs. 
//		DWORD fNull  :1;				If this member is TRUE, null bytes are discarded when received.
//		DWORD fRtsControl  :2;			The RTS (request-to-send) flow control. This member can be one of the following values.
//		DWORD fAbortOnError  :1;		If this member is TRUE, the driver terminates all read and write operations with an error status if an error occurs. The driver will not 
//											accept any further communications operations until the application has acknowledged the error by calling the ClearCommError function. 
//		DWORD fDummy2  :17;				Reserved; do not use.
//		WORD  wReserved;				Reserved; must be zero.
//		WORD  XonLim;					The minimum number of bytes allowed in the input buffer before flow control is activated to inhibit the sender. Note that the sender may 
//											transmit characters after the flow control signal has been activated, so this value should never be zero. This assumes that either XON/XOFF, 
//										RTS, or DTR input flow control is specified in fInX, fRtsControl, or fDtrControl. 
//		WORD  XoffLim;					The maximum number of bytes allowed in the input buffer before flow control is activated to allow transmission by the sender. This assumes 
//											that either XON/XOFF, RTS, or DTR input flow control is specified in fInX, fRtsControl, or fDtrControl. The maximum number of bytes allowed is 
//											calculated by subtracting this value from the size, in bytes, of the input buffer. 
//		BYTE  ByteSize;					The number of bits in the bytes transmitted and received.
//		BYTE  Parity;					The parity scheme to be used. This member can be one of the following values.
//		BYTE  StopBits;					The number of stop bits to be used. This member can be one of the following values.
//		char  XonChar;					The value of the XON character for both transmission and reception.			
//		char  XoffChar;					The value of the XOFF character for both transmission and reception.
//		char  ErrorChar;				The value of the character used to replace bytes received with a parity error.
//		char  EofChar;					The value of the character used to signal the end of data.
//		char  EvtChar;					The value of the character used to signal an event.
//		WORD  wReserved1;				Reserved; do not use.
//	} DCB, *LPDCB;

	DebugOut << "DCM.BaudRate:  " << int(m_Dcb.BaudRate)	<< std::endl;
	DebugOut << "DCM.ByteSize:  " << int(m_Dcb.ByteSize)	<< std::endl;
	DebugOut << "DCM.Parity:    " << int(m_Dcb.Parity)		<< std::endl;
	DebugOut << "DCM.StopBits:  " << int(m_Dcb.StopBits)	<< std::endl;
// T E S T		DebugOut << "DCM.EofChar:   " << int(m_Dcb.EofChar)		<< std::endl;
}
/*************************************************************************************************/
bool	CWin32_Serial::configureUART(DWORD BaudRate, BYTE ByteSize, BYTE Parity, BYTE StopBits)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		// m_lastError = GetLastError();
		return false;
	}

	BOOL	retValue;

	m_Dcb.BaudRate	= BaudRate;
	m_Dcb.ByteSize	= ByteSize;
	m_Dcb.Parity	= Parity;
	m_Dcb.StopBits	= StopBits;
// T E S T	m_Dcb.EofChar	= '\n';
// T E S T		m_Dcb.EvtChar	= '\n';
//	BOOL WINAPI SetCommState(			
//		__in		HANDLE hFile,								A handle to the communications device. The CreateFile function returns this handle. 
//		__in		LPDCB lpDCB);								A pointer to a DCB structure that contains the configuration information for the specified communications device.
//	Return Value:												If the function succeeds, the return value is nonzero.
//																If the function fails, the return value is zero. To get extended error information, call GetLastError. 
	retValue = SetCommState(m_hSerPort, &m_Dcb);

	if (FALSE == retValue)
	{
		m_lastError = GetLastError();
		return false;
	}
	return true;
}
/*************************************************************************************************/
bool	CWin32_Serial::setReadTimeout(DWORD nTimeout)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		// m_lastError = GetLastError();
		return false;
	}

	BOOL	retValue;

	m_Timeout.ReadIntervalTimeout			= nTimeout;
	m_Timeout.ReadTotalTimeoutMultiplier	= nTimeout;
	m_Timeout.ReadTotalTimeoutConstant		= nTimeout;

//	BOOL WINAPI SetCommTimeouts(								Sets the time-out parameters for all read and write operations on a specified communications device.			
//		__in  HANDLE hFile,										A handle to the communications device. The CreateFile function returns this handle. 
//		__in  LPCOMMTIMEOUTS lpCommTimeouts);					A pointer to a COMMTIMEOUTS structure that contains the new time-out values. 
//	Return Value:												If the function succeeds, the return value is nonzero.
//																If the function fails, the return value is zero. To get extended error information, call GetLastError. 
	retValue = SetCommTimeouts(m_hSerPort, &m_Timeout);
	if ( FALSE == retValue)
	{
		m_lastError = GetLastError();
		return false;
	}

	return true;
}
/************************************************ S c h r e i b e n *************************************************/
bool	CWin32_Serial::write(BYTE data)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		// m_lastError = GetLastError();
		return READ_FAIL;
	}

//	typedef struct _OVERLAPPED 
//	{
//		ULONG_PTR Internal;										The error code for the I/O request. When the request is issued, the system sets this member to 
//																STATUS_PENDING to indicate that the operation has not yet started. When the request is completed, 
//																the system sets this member to the error code for the completed request. 
//																The Internal member was originally reserved for system use and its behavior may change. 
//		ULONG_PTR InternalHigh;									The number of bytes transferred for the I/O request. The system sets this member if the request 
//																is completed without errors. 
//																The InternalHigh member was originally reserved for system use and its behavior may change. 
//		union
//			{
//			struct
//				{
//				DWORD Offset;									The low-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				DWORD OffsetHigh;								The high-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				};
//			PVOID  Pointer;										Reserved for system use; do not use after initialization to zero.
//			} ;
//		HANDLE    hEvent;										A handle to the event that will be set to a signaled state by the system when the operation has completed. 
//																The user must initialize this member either to zero or a valid event handle using the CreateEvent function 
//																before passing this structure to any overlapped functions. This event can then be used to synchronize 
//																simultaneous I/O requests for a device. For additional information, see Remarks. 
//																Functions such as ReadFile and WriteFile set this handle to the nonsignaled state before they begin an I/O 
//																operation. When the operation has completed, the handle is set to the signaled state. 
//																Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the 
//																nonsignaled state. Therefore, you should use a manual reset event; if you use an auto-reset event, your 
//																application can stop responding if you wait for the operation to complete and then call GetOverlappedResult 
//																with the bWait parameter set to TRUE.
//	} OVERLAPPED, *LPOVERLAPPED;
	OVERLAPPED		sWriteOverlapped;
	DWORD			nAnzahlGeschriebenerZeichen = 0;
	BOOL			retValue;
	DWORD			nRetValue;									// Rückgabewert von WaitForSingleEvent() ...
		
//	BOOL WINAPI WriteFile(
//		__in		HANDLE hFile,								A handle to the file or I/O device (for example, a file, file stream, physical disk, volume, console buffer, 
//																tape drive, socket, communications resource, mailslot, or pipe). 
//																The hFile parameter must have been created with the write access. For more information, see 
//																Generic Access Rights and File Security and Access Rights. 
//																For asynchronous write operations, hFile can be any handle opened with the CreateFile function using the 
//																FILE_FLAG_OVERLAPPED flag or a socket handle returned by the socket or accept function. 
//		__in		LPCVOID lpBuffer,							A pointer to the buffer containing the data to be written to the file or device.
//																This buffer must remain valid for the duration of the write operation. The caller must not use this buffer 
//																until the write operation is completed.
//		__in		DWORD nNumberOfBytesToWrite,				The number of bytes to be written to the file or device.
//																A value of zero specifies a null write operation. The behavior of a null write operation depends on the 
//																underlying file system or communications technology.
//		__out_opt	LPDWORD lpNumberOfBytesWritten,				A pointer to the variable that receives the number of bytes written when using a synchronous hFile parameter. 
//																WriteFile sets this value to zero before doing any work or error checking. Use NULL for this parameter if 
//																this is an asynchronous operation to avoid potentially erroneous results. 
//																This parameter can be NULL only when the lpOverlapped parameter is not NULL. 
//		__inout_opt	LPOVERLAPPED lpOverlapped);					A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED, 
//																otherwise this parameter can be NULL. 
//	Return Value:												If the function succeeds, the return value is nonzero (TRUE).
//																If the function fails, or is completing asynchronously, the return value is zero (FALSE). To get extended error 
//																information, call the GetLastError function. 
	if (m_bAsync)
	{
		if (NULL == m_hWriteEvent)
		{
			return false;
		}
		memset(&sWriteOverlapped, 0, sizeof(sWriteOverlapped));
		sWriteOverlapped.hEvent = m_hWriteEvent;
		retValue = 	WriteFile(m_hSerPort, &data, 1, NULL, &sWriteOverlapped);
		if (FALSE == retValue)
		{
			m_lastError = GetLastError();
			if (ERROR_IO_PENDING != m_lastError)
				return false;
		}

		nRetValue = WaitForSingleObject(m_hWriteEvent,INFINITE);
		if (0xFFFFFFFF == nRetValue)
		{
			#ifdef __AFX_H__
			TRACE("CWin32_Serial::write() beendet. Fehler bei WaitForSingleObject().");
			#endif
			return false;
		}

//	BOOL WINAPI GetOverlappedResult(							Retrieves the results of an overlapped operation on the specified file, named pipe, or communications device.
//		__in   HANDLE hFile,									A handle to the file, named pipe, or communications device. This is the same handle that was specified when the overlapped operation was started by 
//																a call to the ReadFile, WriteFile, ConnectNamedPipe, TransactNamedPipe, DeviceIoControl, or WaitCommEvent function. 
//		__in   LPOVERLAPPED lpOverlapped,						A pointer to an OVERLAPPED structure that was specified when the overlapped operation was started.
//		__out  LPDWORD lpNumberOfBytesTransferred,				A pointer to a variable that receives the number of bytes that were actually transferred by a read or write operation. For a TransactNamedPipe 
//																operation, this is the number of bytes that were read from the pipe. For a DeviceIoControl operation, this is the number of bytes of output data 
//																returned by the device driver. For a ConnectNamedPipe or WaitCommEvent operation, this value is undefined. 
//		__in   BOOL bWait);										If this parameter is TRUE, the function does not return until the operation has been completed. If this parameter is FALSE and the operation is 
//																still pending, the function returns FALSE and the GetLastError function returns ERROR_IO_INCOMPLETE. 
//		WaitForSingleObject(m_hSerPort,INFINITE);
//		retValue = GetOverlappedResult(m_hSerPort, &sWriteOverlapped, &nAnzahlGeschriebenerZeichen, TRUE);
		retValue = GetOverlappedResult(m_hSerPort, &sWriteOverlapped, &nAnzahlGeschriebenerZeichen, FALSE);
	}
	else
		retValue = 	WriteFile(m_hSerPort, &data, 1, &nAnzahlGeschriebenerZeichen, NULL);
	if (FALSE == retValue)
	{
		m_lastError = GetLastError();
		return false;
	}
	if (0 == nAnzahlGeschriebenerZeichen)
		return false;

	return true;
}
/*************************************************************************************************/
bool	CWin32_Serial::write(BYTE data[], DWORD ByteToWrite, DWORD &BytesWritten)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		// m_lastError = GetLastError();
		return READ_FAIL;
	}

//	typedef struct _OVERLAPPED 
//	{
//		ULONG_PTR Internal;										The error code for the I/O request. When the request is issued, the system sets this member to 
//																STATUS_PENDING to indicate that the operation has not yet started. When the request is completed, 
//																the system sets this member to the error code for the completed request. 
//																The Internal member was originally reserved for system use and its behavior may change. 
//		ULONG_PTR InternalHigh;									The number of bytes transferred for the I/O request. The system sets this member if the request 
//																is completed without errors. 
//																The InternalHigh member was originally reserved for system use and its behavior may change. 
//		union
//			{
//			struct
//				{
//				DWORD Offset;									The low-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				DWORD OffsetHigh;								The high-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				};
//			PVOID  Pointer;										Reserved for system use; do not use after initialization to zero.
//			};
//		HANDLE    hEvent;										A handle to the event that will be set to a signaled state by the system when the operation has completed. 
//																The user must initialize this member either to zero or a valid event handle using the CreateEvent function 
//																before passing this structure to any overlapped functions. This event can then be used to synchronize 
//																simultaneous I/O requests for a device. For additional information, see Remarks. 
//																Functions such as ReadFile and WriteFile set this handle to the nonsignaled state before they begin an I/O 
//																operation. When the operation has completed, the handle is set to the signaled state. 
//																Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the 
//																nonsignaled state. Therefore, you should use a manual reset event; if you use an auto-reset event, your 
//																application can stop responding if you wait for the operation to complete and then call GetOverlappedResult 
//																with the bWait parameter set to TRUE.
//	} OVERLAPPED, *LPOVERLAPPED;
	OVERLAPPED		sWriteOverlapped;
	BOOL			retValue;
	DWORD			nRetValue;									// Rückgabewert von WaitForSingleEvent() ...
		
//	BOOL WINAPI WriteFile(
//		__in		HANDLE hFile,								A handle to the file or I/O device (for example, a file, file stream, physical disk, volume, console buffer, 
//																tape drive, socket, communications resource, mailslot, or pipe). 
//																The hFile parameter must have been created with the write access. For more information, see 
//																Generic Access Rights and File Security and Access Rights. 
//																For asynchronous write operations, hFile can be any handle opened with the CreateFile function using the 
//																FILE_FLAG_OVERLAPPED flag or a socket handle returned by the socket or accept function. 
//		__in		LPCVOID lpBuffer,							A pointer to the buffer containing the data to be written to the file or device.
//																This buffer must remain valid for the duration of the write operation. The caller must not use this buffer 
//																until the write operation is completed.
//		__in		DWORD nNumberOfBytesToWrite,				The number of bytes to be written to the file or device.
//																A value of zero specifies a null write operation. The behavior of a null write operation depends on the 
//																underlying file system or communications technology.
//		__out_opt	LPDWORD lpNumberOfBytesWritten,				A pointer to the variable that receives the number of bytes written when using a synchronous hFile parameter. 
//																WriteFile sets this value to zero before doing any work or error checking. Use NULL for this parameter if 
//																this is an asynchronous operation to avoid potentially erroneous results. 
//																This parameter can be NULL only when the lpOverlapped parameter is not NULL. 
//		__inout_opt	LPOVERLAPPED lpOverlapped);					A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED, 
//																otherwise this parameter can be NULL. 
//	Return Value:												If the function succeeds, the return value is nonzero (TRUE).
//																If the function fails, or is completing asynchronously, the return value is zero (FALSE). To get extended error 
//																information, call the GetLastError function. 
	if (m_bAsync)
	{
		if (NULL == m_hWriteEvent)
		{
			return false;
		}
		memset(&sWriteOverlapped, 0, sizeof(sWriteOverlapped));
		sWriteOverlapped.hEvent = m_hWriteEvent;
		retValue = 	WriteFile(m_hSerPort, data, ByteToWrite, NULL, &sWriteOverlapped);
		if (FALSE == retValue)
		{
			m_lastError = GetLastError();
			if (ERROR_IO_PENDING != m_lastError)
				return false;
		}

		nRetValue = WaitForSingleObject(m_hWriteEvent,INFINITE);
		if (0xFFFFFFFF == nRetValue)
		{
			#ifdef __AFX_H__
			TRACE("CWin32_Serial::write() beendet. Fehler bei WaitForSingleObject().");
			#endif
			return false;
		}


//	BOOL WINAPI GetOverlappedResult(							Retrieves the results of an overlapped operation on the specified file, named pipe, or communications device.
//		__in   HANDLE hFile,									A handle to the file, named pipe, or communications device. This is the same handle that was specified when the overlapped operation was started by 
//																a call to the ReadFile, WriteFile, ConnectNamedPipe, TransactNamedPipe, DeviceIoControl, or WaitCommEvent function. 
//		__in   LPOVERLAPPED lpOverlapped,						A pointer to an OVERLAPPED structure that was specified when the overlapped operation was started.
//		__out  LPDWORD lpNumberOfBytesTransferred,				A pointer to a variable that receives the number of bytes that were actually transferred by a read or write operation. For a TransactNamedPipe 
//																operation, this is the number of bytes that were read from the pipe. For a DeviceIoControl operation, this is the number of bytes of output data 
//																returned by the device driver. For a ConnectNamedPipe or WaitCommEvent operation, this value is undefined. 
//		__in   BOOL bWait);										If this parameter is TRUE, the function does not return until the operation has been completed. If this parameter is FALSE and the operation is 
//																still pending, the function returns FALSE and the GetLastError function returns ERROR_IO_INCOMPLETE. 
//		WaitForSingleObject(m_hSerPort,INFINITE);
//		retValue = GetOverlappedResult(m_hSerPort, &sWriteOverlapped, &BytesWritten, TRUE);
		retValue = GetOverlappedResult(m_hSerPort, &sWriteOverlapped, &BytesWritten, FALSE);
	}
	else
		retValue = 	WriteFile(m_hSerPort, data, ByteToWrite, &BytesWritten, NULL);
	if (FALSE == retValue)
	{
		m_lastError = GetLastError();
		return false;
	}
	if (ByteToWrite != BytesWritten)
		return false;

	return true;
}
/************************************************ L e s e n *************************************************/
CWin32_Serial::eReadStatus	CWin32_Serial::read(BYTE &data)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		return READ_FAIL;
	}

//	typedef struct _OVERLAPPED 
//	{
//		ULONG_PTR Internal;										The error code for the I/O request. When the request is issued, the system sets this member to 
//																STATUS_PENDING to indicate that the operation has not yet started. When the request is completed, 
//																the system sets this member to the error code for the completed request. 
//																The Internal member was originally reserved for system use and its behavior may change. 
//		ULONG_PTR InternalHigh;									The number of bytes transferred for the I/O request. The system sets this member if the request 
//																is completed without errors. 
//																The InternalHigh member was originally reserved for system use and its behavior may change. 
//		union
//			{
//			struct
//				{
//				DWORD Offset;									The low-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				DWORD OffsetHigh;								The high-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				} ;
//			PVOID  Pointer;										Reserved for system use; do not use after initialization to zero.
//			};
//		HANDLE    hEvent;										A handle to the event that will be set to a signaled state by the system when the operation has completed. 
//																The user must initialize this member either to zero or a valid event handle using the CreateEvent function 
//																before passing this structure to any overlapped functions. This event can then be used to synchronize 
//																simultaneous I/O requests for a device. For additional information, see Remarks. 
//																Functions such as ReadFile and WriteFile set this handle to the nonsignaled state before they begin an I/O 
//																operation. When the operation has completed, the handle is set to the signaled state. 
//																Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the 
//																nonsignaled state. Therefore, you should use a manual reset event; if you use an auto-reset event, your 
//																application can stop responding if you wait for the operation to complete and then call GetOverlappedResult 
//																with the bWait parameter set to TRUE.
//	} OVERLAPPED, *LPOVERLAPPED;
	OVERLAPPED		sReadOverlapped;							// Overlapped-Struktur für asynchrones Lesen
	DWORD			nAnzahlGeleseneZeichen = 0;					// Anzahl der durch ReadFile() gelesene Zeichen (0 oder 1)
	BOOL			bRetValue;									// Rückgabewert von ReadFile() ...
	DWORD			nRetValue;									// Rückgabewert von WaitForSingleEvent() ...

	if (m_bAsync)
	{
		if (NULL == m_hReadEvent)
		{
			return READ_FAIL;
		}
		memset(&sReadOverlapped, 0, sizeof(sReadOverlapped));
		sReadOverlapped.hEvent = m_hReadEvent;

//	BOOL WINAPI ReadFile(
//		__in		HANDLE hFile,								A handle to the device (for example, a file, file stream, physical disk, volume, console buffer, 
//																tape drive, socket, communications resource, mailslot, or pipe).
//																The hFile parameter must have been created with read access. For more information, see 
//																Generic Access Rights and File Security and Access Rights. 
//																For asynchronous read operations, hFile can be any handle that is opened with the FILE_FLAG_OVERLAPPED 
//																flag by the CreateFile function, or a socket handle returned by the socket or accept function. 
//		__out		LPVOID lpBuffer,							A pointer to the buffer that receives the data read from a file or device.
//																This buffer must remain valid for the duration of the read operation. The caller must not use this buffer 
//																until the read operation is completed.
//		__in		DWORD nNumberOfBytesToRead,					The maximum number of bytes to be read.
//		__out_opt	LPDWORD lpNumberOfBytesRead,				A pointer to the variable that receives the number of bytes read when using a synchronous hFile parameter. 
//																ReadFile sets this value to zero before doing any work or error checking. 
//																Use NULL for this parameter if this is an asynchronous operation to avoid potentially erroneous results. 
//																This parameter can be NULL only when the lpOverlapped parameter is not NULL. 
//		__inout_opt	LPOVERLAPPED lpOverlapped);					A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED, otherwise it can be NULL. 
//																If hFile is opened with FILE_FLAG_OVERLAPPED, the lpOverlapped parameter must point to a valid and unique OVERLAPPED structure, 
//																otherwise the function can incorrectly report that the read operation is complete. 
//																For an hFile that supports byte offsets, if you use this parameter you must specify a byte offset at which to start reading from the file or device. 
//																This offset is specified by setting the Offset and OffsetHigh members of the OVERLAPPED structure. 
//																For an hFile that does not support byte offsets, Offset and OffsetHigh are ignored.

		bRetValue = ReadFile(m_hSerPort, &data, 1, NULL, &sReadOverlapped);
		if (FALSE == bRetValue)
		{
			m_lastError = GetLastError();
			if (ERROR_IO_PENDING != m_lastError)
				return READ_FAIL;
		}

//	DWORD WINAPI WaitForSingleObject(							Waits until the specified object is in the signaled state or the time-out interval elapses.
//		__in  HANDLE hHandle,									A handle to the object. For a list of the object types whose handles can be specified, see the following Remarks section. 
//																If this handle is closed while the wait is still pending, the function's behavior is undefined.
//																The handle must have the SYNCHRONIZE access right. For more information, see Standard Access Rights. 
//		__in  DWORD dwMilliseconds);							The time-out interval, in milliseconds. If a nonzero value is specified, the function waits until the object is signaled 
//																or the interval elapses. If dwMilliseconds is zero, the function does not enter a wait state if the object is not signaled; 
//																it always returns immediately. If dwMilliseconds is INFINITE, the function will return only when the object is signaled. 
//
//	Die Version WaitForSingleObject(m_hSerPort, INFINITE); funktioniert hier nicht, da auch ein erfolgreich gesendetes Datenpaket einen signalisierten Zustand darstellt!

		nRetValue = WaitForSingleObject(m_hReadEvent,INFINITE);
		if (0xFFFFFFFF == nRetValue)
		{
			#ifdef __AFX_H__
			TRACE("Lese-Tread beendet. Fehler bei WaitForSingleObject().");
			#endif
			return READ_FAIL;
		}

//	BOOL WINAPI GetOverlappedResult(							Retrieves the results of an overlapped operation on the specified file, named pipe, or communications device.
//		__in   HANDLE hFile,									A handle to the file, named pipe, or communications device. This is the same handle that was specified when the overlapped operation was started by 
//																a call to the ReadFile, WriteFile, ConnectNamedPipe, TransactNamedPipe, DeviceIoControl, or WaitCommEvent function. 
//		__in   LPOVERLAPPED lpOverlapped,						A pointer to an OVERLAPPED structure that was specified when the overlapped operation was started.
//		__out  LPDWORD lpNumberOfBytesTransferred,				A pointer to a variable that receives the number of bytes that were actually transferred by a read or write operation. For a TransactNamedPipe 
//																operation, this is the number of bytes that were read from the pipe. For a DeviceIoControl operation, this is the number of bytes of output data 
//																returned by the device driver. For a ConnectNamedPipe or WaitCommEvent operation, this value is undefined. 
//		__in   BOOL bWait);										If this parameter is TRUE, the function does not return until the operation has been completed. If this parameter is FALSE and the operation is 
//																still pending, the function returns FALSE and the GetLastError function returns ERROR_IO_INCOMPLETE. 
		bRetValue = GetOverlappedResult(m_hSerPort, &sReadOverlapped, &nAnzahlGeleseneZeichen, FALSE);
		if (FALSE == bRetValue)
		{
			m_lastError = GetLastError();
			if (ERROR_IO_INCOMPLETE != m_lastError)
			{
				#ifdef __AFX_H__
				TRACE("Lese-Tread beendet. Fehler bei GetOverlappedResult().");
				#endif
				return READ_FAIL;
			}
		}
	}
	else
	{
		bRetValue = ReadFile(m_hSerPort, &data, 1, &nAnzahlGeleseneZeichen, NULL);
		if (FALSE == bRetValue)
		{
			m_lastError = GetLastError();
			if (ERROR_OPERATION_ABORTED == m_lastError)
				return READ_ABORTED;

			return READ_FAIL;
		}
	}
	if (0 == nAnzahlGeleseneZeichen)
		return READ_TIMEOUT;

	return READ_OK;
}
/*************************************************************************************************/
// todo: noch zu implementieren
/*
CWin32_Serial::eReadStatus	CWin32_Serial::read(BYTE data[], DWORD MaxByte, DWORD &BytesRead)
{
	OVERLAPPED		sReadOverlapped;							// Overlapped-Struktur für asynchrones Lesen
	DWORD			nAnzahlGeleseneZeichen = 0;					// Anzahl der durch ReadFile() gelesene Zeichen (0 oder 1)
	BOOL			bRetValue;									// Rückgabewert von ReadFile() ...
	DWORD			nRetValue;									// Rückgabewert von WaitForSingleEvent() ...
	BYTE			cZeichen;									// Das eine Zeichen das durch ReadFile() gelesen wird
	BYTE			*pPuffer = data;							// Schreibzeiger, der durch den acReadPuffer[] läuft
	int				nTimeout = 0;								// Schleifenzähler für nStringTimeout

	bRetValue = ReadFile(m_hSerPort, data, MaxByte, &BytesRead, NULL);


	return READ_OK;
}*/
/*************************************************************************************************/
//	Eine Zeile lesen. CR wird überlesen, LF dient als Zeilenendemarkierung ('\n').
//	Vehält sich damit wie cin.getline().
//
//
//		A C H T U N G ! nStringTimeout muss noch getestet werden
//
CWin32_Serial::eReadStatus	CWin32_Serial::readline(BYTE data[], unsigned int nMaxByte, unsigned int &nStringLaenge, int nStringTimeout)
{
	if (INVALID_HANDLE_VALUE == m_hSerPort)
	{
		return READ_FAIL;
	}

//	typedef struct _OVERLAPPED 
//	{
//		ULONG_PTR Internal;										The error code for the I/O request. When the request is issued, the system sets this member to 
//																STATUS_PENDING to indicate that the operation has not yet started. When the request is completed, 
//																the system sets this member to the error code for the completed request. 
//																The Internal member was originally reserved for system use and its behavior may change. 
//		ULONG_PTR InternalHigh;									The number of bytes transferred for the I/O request. The system sets this member if the request 
//																is completed without errors. 
//																The InternalHigh member was originally reserved for system use and its behavior may change. 
//		union
//			{
//			struct
//				{
//				DWORD Offset;									The low-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				DWORD OffsetHigh;								The high-order portion of the file position at which to start the I/O request, as specified by the user. 
//																This member is nonzero only when performing I/O requests on a seeking device that supports the concept 
//																of an offset (also referred to as a file pointer mechanism), such as a file. Otherwise, this member must be zero.
//				} ;
//			PVOID  Pointer;										Reserved for system use; do not use after initialization to zero.
//			} ;
//		HANDLE    hEvent;										A handle to the event that will be set to a signaled state by the system when the operation has completed. 
//																The user must initialize this member either to zero or a valid event handle using the CreateEvent function 
//																before passing this structure to any overlapped functions. This event can then be used to synchronize 
//																simultaneous I/O requests for a device. For additional information, see Remarks. 
//																Functions such as ReadFile and WriteFile set this handle to the nonsignaled state before they begin an I/O 
//																operation. When the operation has completed, the handle is set to the signaled state. 
//																Functions such as GetOverlappedResult and the synchronization wait functions reset auto-reset events to the 
//																nonsignaled state. Therefore, you should use a manual reset event; if you use an auto-reset event, your 
//																application can stop responding if you wait for the operation to complete and then call GetOverlappedResult 
//																with the bWait parameter set to TRUE.
//	} OVERLAPPED, *LPOVERLAPPED;
	OVERLAPPED		sReadOverlapped;							// Overlapped-Struktur für asynchrones Lesen
	DWORD			nAnzahlGeleseneZeichen = 0;					// Anzahl der durch ReadFile() gelesene Zeichen (0 oder 1)
	BOOL			bRetValue;									// Rückgabewert von ReadFile() ...
	DWORD			nRetValue;									// Rückgabewert von WaitForSingleEvent() ...
	BYTE			cZeichen;									// Das eine Zeichen das durch ReadFile() gelesen wird
	BYTE			*pPuffer = data;							// Schreibzeiger, der durch den acReadPuffer[] läuft
	int				nTimeout = 0;								// Schleifenzähler für nStringTimeout
	memset(data, 0, nMaxByte);									// Vorsorglich mal Stringende in alle Speicherstellen schreiben.
	nStringLaenge = 0;											// Gesamtzahl gelesener Zeichen (in der Zeile)
	do 
	{
		nAnzahlGeleseneZeichen = 0;								// Anzahl der durch ReadFile() gelesene Zeichen (0 oder 1)
		if (m_bAsync)
		{														// Falls Asynchrone Operation gewählt OVERLAPPED Datenstruktur initialisieren
			if (NULL == m_hReadEvent)
			{
				return READ_FAIL;
			}
			memset(&sReadOverlapped, 0, sizeof(sReadOverlapped));
			sReadOverlapped.hEvent = m_hReadEvent;

//	BOOL WINAPI ReadFile(
//		__in		HANDLE hFile,								A handle to the device (for example, a file, file stream, physical disk, volume, console buffer, 
//																tape drive, socket, communications resource, mailslot, or pipe).
//																The hFile parameter must have been created with read access. For more information, see 
//																Generic Access Rights and File Security and Access Rights. 
//																For asynchronous read operations, hFile can be any handle that is opened with the FILE_FLAG_OVERLAPPED 
//																flag by the CreateFile function, or a socket handle returned by the socket or accept function. 
//		__out		LPVOID lpBuffer,							A pointer to the buffer that receives the data read from a file or device.
//																This buffer must remain valid for the duration of the read operation. The caller must not use this buffer 
//																until the read operation is completed.
//		__in		DWORD nNumberOfBytesToRead,					The maximum number of bytes to be read.
//		__out_opt	LPDWORD lpNumberOfBytesRead,				A pointer to the variable that receives the number of bytes read when using a synchronous hFile parameter. 
//																ReadFile sets this value to zero before doing any work or error checking. 
//																Use NULL for this parameter if this is an asynchronous operation to avoid potentially erroneous results. 
//																This parameter can be NULL only when the lpOverlapped parameter is not NULL. 
//		__inout_opt	LPOVERLAPPED lpOverlapped);					A pointer to an OVERLAPPED structure is required if the hFile parameter was opened with FILE_FLAG_OVERLAPPED, otherwise it can be NULL. 
//																If hFile is opened with FILE_FLAG_OVERLAPPED, the lpOverlapped parameter must point to a valid and unique OVERLAPPED structure, 
//																otherwise the function can incorrectly report that the read operation is complete. 
//																For an hFile that supports byte offsets, if you use this parameter you must specify a byte offset at which to start reading from the file or device. 
//																This offset is specified by setting the Offset and OffsetHigh members of the OVERLAPPED structure. 
//																For an hFile that does not support byte offsets, Offset and OffsetHigh are ignored. 

			bRetValue = ReadFile(m_hSerPort, &cZeichen, 1, NULL, &sReadOverlapped);
			if (FALSE == bRetValue)
			{
				m_lastError = GetLastError();
				if (ERROR_IO_PENDING != m_lastError)
					return READ_FAIL;
			}

//	DWORD WINAPI WaitForSingleObject(							Waits until the specified object is in the signaled state or the time-out interval elapses.
//		__in  HANDLE hHandle,									A handle to the object. For a list of the object types whose handles can be specified, see the following Remarks section. 
//																If this handle is closed while the wait is still pending, the function's behavior is undefined.
//																The handle must have the SYNCHRONIZE access right. For more information, see Standard Access Rights. 
//		__in  DWORD dwMilliseconds);							The time-out interval, in milliseconds. If a nonzero value is specified, the function waits until the object is signaled 
//																or the interval elapses. If dwMilliseconds is zero, the function does not enter a wait state if the object is not signaled; 
//																it always returns immediately. If dwMilliseconds is INFINITE, the function will return only when the object is signaled. 
//
//	Die Version WaitForSingleObject(m_hSerPort, INFINITE); funktioniert hier nicht, da auch ein erfolgreich gesendetes Datenpaket einen signalisierten Zustand darstellt!

			nRetValue = WaitForSingleObject(m_hReadEvent,INFINITE);
			if (0xFFFFFFFF == nRetValue)
			{
				#ifdef __AFX_H__
				TRACE("Lese-Tread beendet. Fehler bei WaitForSingleObject().");
				#endif
				return READ_FAIL;
			}

//	BOOL WINAPI GetOverlappedResult(							Retrieves the results of an overlapped operation on the specified file, named pipe, or communications device.
//		__in   HANDLE hFile,									A handle to the file, named pipe, or communications device. This is the same handle that was specified when the overlapped operation was started by 
//																a call to the ReadFile, WriteFile, ConnectNamedPipe, TransactNamedPipe, DeviceIoControl, or WaitCommEvent function. 
//		__in   LPOVERLAPPED lpOverlapped,						A pointer to an OVERLAPPED structure that was specified when the overlapped operation was started.
//		__out  LPDWORD lpNumberOfBytesTransferred,				A pointer to a variable that receives the number of bytes that were actually transferred by a read or write operation. For a TransactNamedPipe 
//																operation, this is the number of bytes that were read from the pipe. For a DeviceIoControl operation, this is the number of bytes of output data 
//																returned by the device driver. For a ConnectNamedPipe or WaitCommEvent operation, this value is undefined. 
//		__in   BOOL bWait);										If this parameter is TRUE, the function does not return until the operation has been completed. If this parameter is FALSE and the operation is 
//																still pending, the function returns FALSE and the GetLastError function returns ERROR_IO_INCOMPLETE. 
			bRetValue = GetOverlappedResult(m_hSerPort, &sReadOverlapped, &nAnzahlGeleseneZeichen, FALSE);
			if (FALSE == bRetValue)
			{
				m_lastError = GetLastError();
				if (ERROR_IO_INCOMPLETE != m_lastError)
				{
					#ifdef __AFX_H__
					TRACE("Lese-Tread beendet. Fehler bei GetOverlappedResult().");
					#endif
					return READ_FAIL;
				}
			}
		}
		else
		{
			bRetValue = ReadFile(m_hSerPort, &cZeichen, 1, &nAnzahlGeleseneZeichen, NULL);
			if (FALSE == bRetValue)
			{
				m_lastError = GetLastError();
				if (ERROR_OPERATION_ABORTED == m_lastError)
					return READ_ABORTED;

				return READ_FAIL;
			}
		}
		nTimeout++;

		if (0 == nAnzahlGeleseneZeichen)
		{														// Für den Timeout-Fall: Schleife wiederholen.
			if (0 == nStringTimeout)
				continue;

			if (nTimeout <= nStringTimeout)						// Es sei denn, nStringTimeout wurde gesetzt.
				continue;			

			return READ_TIMEOUT;
		}

		nTimeout = 0;											// Timeout zurücksetzen;

	// gelesenes Zeichen auswerten und in Puffer kopieren
		switch (cZeichen) 
		{
		case 0x00:												// 
			continue;
			break;
		case 0x0d:												// CR ignorieren
			continue;
			break;
		case 0x0a:												// LF
			if (0 == nStringLaenge)								// Am Zeilenanfang ingnorieren
				continue;
			*pPuffer = 0;										// String abschließen
			return READ_OK;										// Zurück zum Aufrufen.
			break;

		default:
			*pPuffer = cZeichen;
			*(pPuffer+1) = 0;		
			pPuffer++;
			nStringLaenge++;
		}
	} while (nStringLaenge < nMaxByte-1);
	// bei Pufferüberlauf:
	*(pPuffer-1) = 0;											// String abschließen
	return READ_OK;
}
/*************************************************************************************************/
//void CWin32_Serial::dummy()
//{
//
//	::PostMessageA((HWND) 33, (UINT) 44, (WPARAM) 55, (LPARAM) 66);
//
//}