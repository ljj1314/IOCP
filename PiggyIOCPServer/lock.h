#pragma once

class CMyCtriticalSection
{
public:
	CMyCtriticalSection(void)
	{
		InitializeCriticalSection(&m_sec);
	}
	~CMyCtriticalSection(void)
	{
		DeleteCriticalSection(&m_sec);
	}
	operator CRITICAL_SECTION *()
	{
		return &m_sec;
	}
private:
	CRITICAL_SECTION m_sec;
};

class CGsguard
{
public:
	CGsguard(CRITICAL_SECTION *cs)
	{
		m_cs = cs;
		EnterCriticalSection(m_cs);
	}
	~CGsguard()
	{
		LeaveCriticalSection(m_cs);
	}
private:
	CRITICAL_SECTION *m_cs;
};


class CMyEvent
{
public:
	CMyEvent()
	{
		m_hEvent = ::CreateEvent(NULL,FALSE,FALSE,NULL);
	}
	~CMyEvent()
	{
		CloseHandle(m_hEvent);
	}

	void SetEvent()
	{
		::SetEvent(m_hEvent);
	}

	void Wait()
	{
		WaitForSingleObject(m_hEvent,INFINITE);
	}
private:
	HANDLE m_hEvent;
};

class CMyMutex
{
public:
	CMyMutex()
	{
		m_hMutex = CreateMutex(NULL,FALSE,NULL);
	}
	~CMyMutex()
	{
		CloseHandle(m_hMutex);
	}

	void GetMutex()
	{
		WaitForSingleObject(m_hMutex,INFINITE);
	}

	void Release()
	{
		ReleaseMutex(m_hMutex);
	}

private:
	HANDLE m_hMutex;
};