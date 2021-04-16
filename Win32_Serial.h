#ifndef CWin32_Serial_H
#define CWin32_Serial_H

#include <windows.h>
#include <iostream>
#include <string>

class CWin32_Serial
{
public:
	enum		eReadStatus {READ_OK, READ_FAIL, READ_TIMEOUT, READ_ABORTED};
	// Konstruktor
	CWin32_Serial();
	~CWin32_Serial(void);
	bool		open(std::string deviceName, bool bAsync = false);
	bool		close();
	bool		isOpen();
	bool		isAsynchron();
	eReadStatus	read(BYTE &data);
	bool		write(BYTE data);
//	eReadStatus	read(BYTE data[], DWORD MaxByte, DWORD &BytesRead);
	bool		write(BYTE data[], DWORD ByteToWrite, DWORD &BytesWritten);
	void		printConfiguration(std::ostream &DebugOut = std::cout) const;
	bool		configureUART(DWORD BaudRate = CBR_9600, BYTE ByteSize = 8, BYTE Parity = NOPARITY, BYTE StopBits = ONESTOPBIT);
	bool		setReadTimeout(DWORD nTimeout);
	eReadStatus	readline(BYTE data[], unsigned int nMaxByte, unsigned int &nStringLaenge, int nStringTimeout = 0);

//	void		dummy();

private:
	std::string		m_portName;
	HANDLE			m_hSerPort;					// file handle
	HANDLE			m_hReadEvent;				// Eventobjekt, wartet auf ein Zeichen
	HANDLE			m_hWriteEvent;				// Eventobjekt, soll Problem beim Schreiben lösen
	DCB				m_Dcb;						// device control block
	COMMTIMEOUTS	m_Timeout;					// COMMTIMEOUTS Structure
	DWORD			m_lastError;
	bool			m_bAsync;					// Asynchron Modus?
	friend class CCulAsync;
};

#endif