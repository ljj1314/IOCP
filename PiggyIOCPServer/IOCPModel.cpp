#include "StdAfx.h"
#include "IOCPModel.h"
#include "MainDlg.h"
#include <time.h>
#include"afxmt.h"


typedef map<unsigned int, itemS> UDT_MAP_INT_CHAR;
UDT_MAP_INT_CHAR tagMap;

//记录ringID连续两次读到的时间
mapringID2Time g_mapringID2Time;
CMyCtriticalSection g_section;

mapfilter_time g_mapfilter_time;
CMyCtriticalSection g_secfiter;		//定位数据用

mapfilter_time g_mapatttab_time;
CMyCtriticalSection g_secatttab;		//考勤数据用

mapfilter_heart g_mapheart_time;
CMyCtriticalSection g_secheart;    //心跳数据用

CMyCtriticalSection g_seclog;     //日志专用

int g_nTimeout = 0;					//定位数据超时时间用
int g_nThres = 1;		//考勤数据上传间隔时间

// 每一个处理器上产生多少个线程(为了最大限度的提升服务器性能，详见配套文档)
#define WORKER_THREADS_PER_PROCESSOR 4
// 同时投递的Accept请求的数量(这个要根据实际的情况灵活设置)
#define MAX_POST_ACCEPT              1000     //根据电脑的核数设定
// 传递给Worker线程的退出信号
#define EXIT_CODE                    NULL


// 释放指针和句柄资源的宏

// 释放指针宏
#define RELEASE(x)                      {if(x != NULL ){delete x;x=NULL;}}
// 释放句柄宏
#define RELEASE_HANDLE(x)               {if(x != NULL && x!=INVALID_HANDLE_VALUE){ CloseHandle(x);x = NULL;}}
// 释放Socket宏
#define RELEASE_SOCKET(x)               {if(x !=INVALID_SOCKET) { closesocket(x);x=INVALID_SOCKET;}}
	
CIOCPModel::CIOCPModel(void):
m_nThreads(0),
	m_hShutdownEvent(NULL),
	m_hIOCompletionPort(NULL),
	m_phWorkerThreads(NULL),
	m_strIP(DEFAULT_IP),
	m_nPort(DEFAULT_PORT),
	m_pMain(NULL),
	m_lpfnAcceptEx( NULL ),
	m_pListenContext( NULL )
{
	::CoInitialize(NULL);//初始化Com库
}


CIOCPModel::~CIOCPModel(void)
{
	// 确保资源彻底释放
	this->Stop();
}




///////////////////////////////////////////////////////////////////
// 工作者线程：  为IOCP请求服务的工作者线程
//         也就是每当完成端口上出现了完成数据包，就将之取出来进行处理的线程
///////////////////////////////////////////////////////////////////

DWORD WINAPI CIOCPModel::_WorkerThread(LPVOID lpParam)
{    
	THREADPARAMS_WORKER* pParam = (THREADPARAMS_WORKER*)lpParam;
	CIOCPModel* pIOCPModel = (CIOCPModel*)pParam->pIOCPModel;
	int nThreadNo = (int)pParam->nThreadNo;

	char buff[100] = {0};

	pIOCPModel->_ShowMessage(_T("工作者线程启动，ID: %d."),nThreadNo);

	OVERLAPPED           *pOverlapped = NULL;
	PER_SOCKET_CONTEXT   *pSocketContext = NULL;
	DWORD                dwBytesTransfered = 0;
	DWORD                dwlasttime =  GetTickCount();
	int                  nErrCount = 0;

	// 循环处理请求，知道接收到Shutdown信息为止
	while (WAIT_OBJECT_0 != WaitForSingleObject(pIOCPModel->m_hShutdownEvent, 0))
	{
		BOOL bReturn = GetQueuedCompletionStatus(
			pIOCPModel->m_hIOCompletionPort,
			&dwBytesTransfered,
			(PULONG_PTR)&pSocketContext,
			&pOverlapped,
			INFINITE);

		// 如果收到的是退出标志，则直接退出
		if ( EXIT_CODE==(DWORD)pSocketContext )
		{
			break;
		}

		// 读取传入的参数
		PER_IO_CONTEXT* pIoContext = CONTAINING_RECORD(pOverlapped, PER_IO_CONTEXT, m_Overlapped);


		// 判断是否出现了错误
		if( !bReturn )  
		{  
			DWORD dwErr = GetLastError();

			// 显示一下提示信息
			if( !pIOCPModel->HandleError( pSocketContext,dwErr ) )
			{
				//((CListCtrl *)(pIOCPModel->m_pMain->GetDlgItem(IDC_LIST_MSG)))->DeleteAllItems();
				//break;
				// 释放掉对应的资源
				if(pIOCPModel->m_phWorkerThreads == NULL)
				{
					break;
				}
				else
				{
					pIOCPModel->_RemoveContext( pSocketContext );
					continue;
				}
			}

			if( pIoContext != NULL )
			{
				pIOCPModel->_PostRecv(pIoContext);
				continue;
			}
		}  
		else  
		{  	
			// 判断是否有客户端断开了
			if((0 == dwBytesTransfered) && ( RECV_POSTED==pIoContext->m_OpType || SEND_POSTED==pIoContext->m_OpType))  
			{  
				pIOCPModel->_ShowMessage( _T("客户端 %s:%d 断开连接."),inet_ntoa(pSocketContext->m_ClientAddr.sin_addr), ntohs(pSocketContext->m_ClientAddr.sin_port) );

				sprintf_s(buff,sizeof(buff),_T("客户端 %s:%d 断开连接"),inet_ntoa(pSocketContext->m_ClientAddr.sin_addr), ntohs(pSocketContext->m_ClientAddr.sin_port) );
				pIOCPModel->a.WriteLog(buff, DEFTOOLS::NORMAL_L);

				//shutdown(pIoContext->m_sockAccept,SD_BOTH);
				//closesocket(pIoContext->m_sockAccept);
				//pIOCPModel->_PostRecv(pIoContext);
				// 释放掉对应的资源
				pIOCPModel->_RemoveContext( pSocketContext );

				continue;  
			}  
			else
			{
				switch( pIoContext->m_OpType )  
				{  
					// Accept  
				case ACCEPT_POSTED:
					{ 
						//					pIoContext->m_nTotalBytes = dwBytesTransfered;
						// 为了增加代码可读性，这里用专门的_DoAccept函数进行处理连入请求
						pIOCPModel->_DoAccpet( pSocketContext, pIoContext );
						
					}
					break;

					// RECV
				case RECV_POSTED:
					{
						//					pIoContext->m_nTotalBytes	= dwBytesTransfered;
						// 为了增加代码可读性，这里用专门的_DoRecv函数进行处理接收请求
						if(false == pIOCPModel->_DoRecv( pSocketContext, pIoContext )){
							pIOCPModel->_RemoveContext( pSocketContext );
						}

						//开始连接时发一次
						if(pIoContext->m_dwTick == 0){
							pIOCPModel->_DoSend(pIoContext);
							pIoContext->m_dwTick = GetTickCount();
						}

						//每天发送同步指令一次
						if(GetTickCount()-pIoContext->m_dwTick > 1000*60*60*24)
						{
							bool timeflag = pIOCPModel->_CompareTime();
							if(timeflag)
							{
								pIOCPModel->_DoSend(pIoContext);
								pIoContext->m_dwTick = GetTickCount();
							}
						}
					}
					break;

					// SEND
					// 这里略过不写了，要不代码太多了，不容易理解，Send操作相对来讲简单一些
				case SEND_POSTED:
					{	
						pIoContext->m_OpType = RECV_POSTED;
					}
					break;
				default:
					// 不应该执行到这里
					//TRACE(_T("_WorkThread中的 pIoContext->m_OpType 参数异常.\n"));
					break;
				} //switch
			}//if
		}//if

	}//while

	//TRACE(_T("工作者线程 %d 号退出.\n"),nThreadNo);

	// 释放线程参数
	RELEASE(lpParam);	

	return 0;
}



//====================================================================================
//
//				    系统初始化和终止
//
//====================================================================================




////////////////////////////////////////////////////////////////////
// 初始化WinSock 2.2
bool CIOCPModel::LoadSocketLib()
{    
	WSADATA wsaData;
	int nResult;
	nResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	// 错误(一般都不可能出现)
	if (NO_ERROR != nResult)
	{
		this->_ShowMessage(_T("初始化WinSock 2.2失败！\n"));
		return false; 
	}

	return true;
}

//////////////////////////////////////////////////////////////////
//	启动服务器
bool CIOCPModel::Start()
{
	// 初始化线程互斥量
	InitializeCriticalSection(&m_csContextList);
	InitializeCriticalSection(&m_csProcess);

	//  初始化访问数据库互斥量
	InitializeCriticalSection(&m_csSqlList);

	// 建立系统退出的事件通知
	m_hShutdownEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	// 初始化IOCP
	if (false == _InitializeIOCP())
	{
		this->_ShowMessage(_T("初始化IOCP失败！\n"));
		return false;
	}
	else
	{
		this->_ShowMessage(_T("\nIOCP初始化完毕\n."));
	}

	// 初始化Socket
	if( false==_InitializeListenSocket() )
	{
		this->_ShowMessage(_T("Listen Socket初始化失败！\n"));
		//this->_DeInitialize();
		return false;
	}
	else
	{
		this->_ShowMessage(_T("Listen Socket初始化完毕."));
	}

	this->_ShowMessage(_T("系统准备就绪，等候连接....\n"));

	//////////////////////////////////////////////////////////////////////////
	// 清除客户端列表信息
	this->_ClearContextList();
	//////////////////////////////////////////////////////////////////////////

	a.InitFunc();
	return true;
}


////////////////////////////////////////////////////////////////////
//	开始发送系统退出消息，退出完成端口和线程资源
void CIOCPModel::Stop()
{
	if( m_pListenContext!=NULL && m_pListenContext->m_Socket!=INVALID_SOCKET )
	{
		// 激活关闭消息通知
		SetEvent(m_hShutdownEvent);

		for (int i = 0; i < m_nThreads; i++)
		{
			// 通知所有的完成端口操作退出
			PostQueuedCompletionStatus(m_hIOCompletionPort, 0, (DWORD)EXIT_CODE, NULL);
		}

		// 释放其他资源
		this->_DeInitialize();

		this->_ShowMessage(_T("停止监听\n"));
	}	
}

////////////////////////////////
// 初始化完成端口
bool CIOCPModel::_InitializeIOCP()
{
	// 建立第一个完成端口
	m_hIOCompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0 );

	if ( NULL == m_hIOCompletionPort)
	{
		this->_ShowMessage(_T("建立完成端口失败！错误代码: %d!\n"), WSAGetLastError());
		return false;
	}

	// 根据本机中的处理器数量，建立对应的线程数
	m_nThreads = WORKER_THREADS_PER_PROCESSOR * _GetNoOfProcessors();

	// 为工作者线程初始化句柄
	m_phWorkerThreads = new HANDLE[m_nThreads];

	// 根据计算出来的数量建立工作者线程
	DWORD nThreadID;
	for (int i = 0; i < m_nThreads; i++)
	{
		THREADPARAMS_WORKER* pThreadParams = new THREADPARAMS_WORKER;
		pThreadParams->pIOCPModel = this;
		pThreadParams->nThreadNo  = i+1;
		m_phWorkerThreads[i] = ::CreateThread(0, 0, _WorkerThread, (void *)pThreadParams, 0, &nThreadID);
	}

	//创建处理线程
	/*
	g_bExtiThread = true;
	for (int i = 0;i < DEFAULT_ASQL_THREAD_COUNT;++i)
	{
		hThread[i] = CreateThread(NULL,0,_ProcessThread,this,0,NULL);
	}*/
	mque.OpenMQ(DataIP.c_str(),Catalog.c_str(),"","");
	return true;
}


/////////////////////////////////////////////////////////////////
// 初始化Socket
bool CIOCPModel::_InitializeListenSocket()
{
	// AcceptEx 和 GetAcceptExSockaddrs 的GUID，用于导出函数指针
	GUID GuidAcceptEx = WSAID_ACCEPTEX;  
	GUID GuidGetAcceptExSockAddrs = WSAID_GETACCEPTEXSOCKADDRS; 

	// 服务器地址信息，用于绑定Socket
	struct sockaddr_in ServerAddress;

	// 生成用于监听的Socket的信息
	m_pListenContext = new PER_SOCKET_CONTEXT;

	// 需要使用重叠IO，必须得使用WSASocket来建立Socket，才可以支持重叠IO操作
	m_pListenContext->m_Socket = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (INVALID_SOCKET == m_pListenContext->m_Socket) 
	{
		this->_ShowMessage(_T("初始化Socket失败，错误代码: %d.\n"), WSAGetLastError());
		return false;
	}
	else
	{
		//TRACE(_T("WSASocket() 完成.\n"));
	}

	// 将Listen Socket绑定至完成端口中
	if( NULL== CreateIoCompletionPort( (HANDLE)m_pListenContext->m_Socket, m_hIOCompletionPort,(DWORD)m_pListenContext, 0))  
	{  
		this->_ShowMessage(_T("绑定 Listen Socket至完成端口失败！错误代码: %d/n"), WSAGetLastError());  
		RELEASE_SOCKET( m_pListenContext->m_Socket );
		return false;
	}
	else
	{
		//TRACE(_T("Listen Socket绑定完成端口 完成.\n"));
	}

	// 填充地址信息
	ZeroMemory((char *)&ServerAddress, sizeof(ServerAddress));
	ServerAddress.sin_family = AF_INET;
	// 这里可以绑定任何可用的IP地址，或者绑定一个指定的IP地址 
	//ServerAddress.sin_addr.s_addr = htonl(INADDR_ANY);                      
	ServerAddress.sin_addr.s_addr = inet_addr(m_strIP.GetString());   

	ServerAddress.sin_port = htons(m_nPort);                          

	// 绑定地址和端口
	if (SOCKET_ERROR == bind(m_pListenContext->m_Socket, (struct sockaddr *) &ServerAddress, sizeof(ServerAddress))) 
	{
		this->_ShowMessage(_T("bind()函数执行错误.\n"));
		return false;
	}
	else
	{
		//TRACE(_T("bind() 完成.\n"));
	}

	// 开始进行监听
	if (SOCKET_ERROR == listen(m_pListenContext->m_Socket,SOMAXCONN))
	{
		this->_ShowMessage(_T("Listen()函数执行出现错误.\n"));
		return false;
	}
	else
	{
		TRACE(_T("Listen() 完成.\n"));
	}

	// 使用AcceptEx函数，因为这个是属于WinSock2规范之外的微软另外提供的扩展函数
	// 所以需要额外获取一下函数的指针，
	// 获取AcceptEx函数指针
	DWORD dwBytes = 0;  
	if(SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_Socket, 
		SIO_GET_EXTENSION_FUNCTION_POINTER, 
		&GuidAcceptEx, 
		sizeof(GuidAcceptEx), 
		&m_lpfnAcceptEx, 
		sizeof(m_lpfnAcceptEx), 
		&dwBytes, 
		NULL, 
		NULL))  
	{  
		this->_ShowMessage(_T("WSAIoctl 未能获取AcceptEx函数指针。错误代码: %d\n"), WSAGetLastError()); 
		this->_DeInitialize();
		return false;  
	}  

	// 获取GetAcceptExSockAddrs函数指针，也是同理
	if(SOCKET_ERROR == WSAIoctl(
		m_pListenContext->m_Socket, 
		SIO_GET_EXTENSION_FUNCTION_POINTER, 
		&GuidGetAcceptExSockAddrs,
		sizeof(GuidGetAcceptExSockAddrs), 
		&m_lpfnGetAcceptExSockAddrs, 
		sizeof(m_lpfnGetAcceptExSockAddrs),   
		&dwBytes, 
		NULL, 
		NULL))  
	{  
		this->_ShowMessage(_T("WSAIoctl 未能获取GuidGetAcceptExSockAddrs函数指针。错误代码: %d\n"), WSAGetLastError());  
		this->_DeInitialize();
		return false; 
	}  


	// 为AcceptEx 准备参数，然后投递AcceptEx I/O请求
	for( int i=0;i<MAX_POST_ACCEPT;i++ )
	{
		// 新建一个IO_CONTEXT
		PER_IO_CONTEXT* pAcceptIoContext = m_pListenContext->GetNewIoContext();

		if( false==this->_PostAccept( pAcceptIoContext ) )
		{
			m_pListenContext->RemoveContext(pAcceptIoContext);
			return false;
		}
	}

	this->_ShowMessage(_T("投递 %d 个AcceptEx请求完毕"),MAX_POST_ACCEPT );

	return true;
}

////////////////////////////////////////////////////////////
//	最后释放掉所有资源
void CIOCPModel::_DeInitialize()
{
	g_bExtiThread = false;
	//WaitForSingleObject( hThread_process, INFINITE );
	DeleteCriticalSection(&m_csProcess);
	//RELEASE_HANDLE(hThread_process);
	// 删除客户端列表的互斥量
	DeleteCriticalSection(&m_csContextList);
	// 删除访问数据库的互斥量
	DeleteCriticalSection(&m_csSqlList);

	// 关闭系统退出事件句柄
	RELEASE_HANDLE(m_hShutdownEvent);

	//RELEASE_HANDLE(hThread_heart);
	//RELEASE(hThread_heart);

	// 释放工作者线程句柄指针 
	for( int i=0;i<m_nThreads;i++ )
	{
		RELEASE_HANDLE(m_phWorkerThreads[i]);
	}

	RELEASE(m_phWorkerThreads);
	/*
	// 释放朵朵处理线程句柄指针 
	for( int i=0;i<DEFAULT_ASQL_THREAD_COUNT;i++ )
	{
		RELEASE_HANDLE(hThread[i]);
	}
	*/
	// 关闭IOCP句柄
	RELEASE_HANDLE(m_hIOCompletionPort);

	// 关闭监听Socket
	RELEASE(m_pListenContext);

	this->_ShowMessage(_T("释放资源完毕.\n"));
}


//====================================================================================
//
//				    投递完成端口请求
//
//====================================================================================


//////////////////////////////////////////////////////////////////
// 投递Accept请求
bool CIOCPModel::_PostAccept( PER_IO_CONTEXT* pAcceptIoContext )
{
	ASSERT( INVALID_SOCKET!=m_pListenContext->m_Socket );

	// 准备参数
	DWORD dwBytes = 0;  
	pAcceptIoContext->m_OpType = ACCEPT_POSTED;  
	WSABUF *p_wbuf   = &pAcceptIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &pAcceptIoContext->m_Overlapped;

	// 为以后新连入的客户端先准备好Socket( 这个是与传统accept最大的区别 ) 
	pAcceptIoContext->m_sockAccept  = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);  
	if( INVALID_SOCKET==pAcceptIoContext->m_sockAccept )  
	{  
		_ShowMessage(_T("创建用于Accept的Socket失败！错误代码: %d"), WSAGetLastError()); 
		return false;  
	} 

	// 投递AcceptEx
	if(FALSE == m_lpfnAcceptEx( m_pListenContext->m_Socket, pAcceptIoContext->m_sockAccept, p_wbuf->buf, p_wbuf->len - ((sizeof(SOCKADDR_IN)+16)*2),   
		sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, &dwBytes, p_ol))  
	{  
		if(WSA_IO_PENDING != WSAGetLastError())  
		{  
			_ShowMessage(_T("投递 AcceptEx 请求失败，错误代码: %d"), WSAGetLastError());  
			return false;  
		}  
	} 

	return true;
}

////////////////////////////////////////////////////////////
// 在有客户端连入的时候，进行处理


// 传入的是ListenSocket的Context，复制一份出来给新连入的Socket用
// 原来的Context还是要在上面继续投递下一个Accept请求
//
bool CIOCPModel::_DoAccpet( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext )
{
	//EnterCriticalSection(&m_csSqlList);

	char buff[256] = {0};
	SOCKADDR_IN* ClientAddr = NULL;
	SOCKADDR_IN* LocalAddr = NULL;  
	int remoteLen = sizeof(SOCKADDR_IN), localLen = sizeof(SOCKADDR_IN);  

	///////////////////////////////////////////////////////////////////////////
	// 1. 取得连入客户端的地址信息
	this->m_lpfnGetAcceptExSockAddrs(pIoContext->m_wsaBuf.buf, pIoContext->m_wsaBuf.len - ((sizeof(SOCKADDR_IN)+16)*2),  
		sizeof(SOCKADDR_IN)+16, sizeof(SOCKADDR_IN)+16, (LPSOCKADDR*)&LocalAddr, &localLen, (LPSOCKADDR*)&ClientAddr, &remoteLen);  

	this->_ShowMessage( _T("客户端 %s:%d %d连入"), inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),pIoContext->m_sockAccept);

	sprintf_s(buff, sizeof(buff),_T("客户端 %s:%d 连入"), inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port));
	a.WriteLog(buff, DEFTOOLS::NORMAL_L);


	PER_SOCKET_CONTEXT* pNewSocketContext = new PER_SOCKET_CONTEXT;
	pNewSocketContext->m_Socket           = pIoContext->m_sockAccept;
	memcpy(&(pNewSocketContext->m_ClientAddr), ClientAddr, sizeof(SOCKADDR_IN));

	// 参数设置完毕，将这个Socket和完成端口绑定(这也是一个关键步骤)
	if( false==this->_AssociateWithIOCP( pNewSocketContext ) )
	{
		RELEASE( pNewSocketContext );
		return false;
	}  


	///////////////////////////////////////////////////////////////////////////////////////////////////
	// 3. 继续，建立其下的IoContext，用于在这个Socket上投递第一个Recv数据请求
	PER_IO_CONTEXT* pNewIoContext = pNewSocketContext->GetNewIoContext();
	pNewIoContext->m_OpType       = RECV_POSTED;
	pNewIoContext->m_sockAccept   = pNewSocketContext->m_Socket;
	// 如果Buffer需要保留，就自己拷贝一份出来
	//memcpy( pNewIoContext->m_szBuffer,pIoContext->m_szBuffer,MAX_BUFFER_LEN );

	// 绑定完毕之后，就可以开始在这个Socket上投递完成请求了
	if( false==this->_PostRecv( pNewIoContext) )
	{
		pNewSocketContext->RemoveContext( pNewIoContext );
		return false;
	}

	/////////////////////////////////////////////////////////////////////////////////////////////////
	// 4. 如果投递成功，那么就把这个有效的客户端信息，加入到ContextList中去(需要统一管理，方便释放资源)
	this->_AddToContextList( pNewSocketContext );

	////////////////////////////////////////////////////////////////////////////////////////////////
	// 5. 使用完毕之后，把Listen Socket的那个IoContext重置，然后准备投递新的AcceptEx
	pIoContext->ResetBuffer();

	//LeaveCriticalSection(&m_csSqlList);

	return this->_PostAccept( pIoContext ); 	
}

////////////////////////////////////////////////////////////////////
// 投递接收数据请求
bool CIOCPModel::_PostRecv( PER_IO_CONTEXT* pIoContext )
{
	// 初始化变量
	DWORD dwFlags = 0;
	DWORD dwBytes = 0;
	WSABUF *p_wbuf   = &pIoContext->m_wsaBuf;
	OVERLAPPED *p_ol = &pIoContext->m_Overlapped;

	pIoContext->ResetBuffer();
	pIoContext->m_OpType = RECV_POSTED;

	// 初始化完成后，，投递WSASend请求
	int nBytesRecv = WSARecv( pIoContext->m_sockAccept, p_wbuf, 1, &dwBytes, &dwFlags, p_ol, NULL );

	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		this->_ShowMessage(_T("投递一个WSARecv失败！"));
		return false;
	}
	return true;
}

bool CIOCPModel::_DoSend( PER_IO_CONTEXT* pSendIoContext )
{
	// 初始化变量
	DWORD dwFlags = 0;
	DWORD dwSendNumBytes = 0;
	//WSABUF *p_wbuf   = &pSendIoContext->m_wsaBuf;
	//OVERLAPPED *p_ol = &pSendIoContext->m_Overlapped;

	////	pSendIoContext->ResetBuffer();
	//pSendIoContext->m_OpType = SEND_POSTED;

	//int nBytesRecv  = WSASend( pSendIoContext->m_sockAccept, &pSendIoContext->m_wsaBuf, 1, &dwSendNumBytes, dwFlags,
	//	&pSendIoContext->m_Overlapped, NULL );
	WSABUF lpBuf;

	unsigned char c1[12]={0};
	//sprintf_s(c1,"%d","123123123123123");//(LPWSABUF)c1
	//0A FF 08 29 16 06 28 18 53 20 F7
	c1[0] = 0x0A;
	c1[1] = 0xFF;
	c1[2] = 0x08;
	c1[3] = 0x29;
	SYSTEMTIME st;
	GetLocalTime(&st);
	DectoBCD(st.wYear,c1+4,1);
	DectoBCD(st.wMonth,c1+5,1);
	DectoBCD(st.wDay,c1+6,1);
	DectoBCD(st.wHour,c1+7,1);
	DectoBCD(st.wMinute,c1+8,1);
	DectoBCD(st.wSecond,c1+9,1);
	c1[10] = _Check_Sum(c1,10);
	lpBuf.buf = (char*)c1;
	lpBuf.len = 12;
	pSendIoContext->m_OpType = SEND_POSTED;
	int nBytesRecv  = WSASend( pSendIoContext->m_sockAccept, &lpBuf , 1,&dwSendNumBytes, dwFlags,
		&pSendIoContext->m_Overlapped, NULL );
	// 如果返回值错误，并且错误的代码并非是Pending的话，那就说明这个重叠请求失败了
	if ((SOCKET_ERROR == nBytesRecv) && (WSA_IO_PENDING != WSAGetLastError()))
	{
		this->_ShowMessage(_T("投递WSASend失败！"));
		return false;
	}
	return true;
}

/////////////////////////////////////////////////////////////////
// 在有接收的数据到达的时候，进行处理
bool CIOCPModel::_DoRecv( PER_SOCKET_CONTEXT* pSocketContext, PER_IO_CONTEXT* pIoContext )
{
	char local_time[50];
	unsigned char addr[4] = {0};
	unsigned int addrHex;
	char reade[32] = {0};
	char buff[256] = {0};
	long lasttime =0;
	SOCKADDR_IN* ClientAddr = &pSocketContext->m_ClientAddr;
	CMainDlg *list_box = (CMainDlg *)m_pMain;

	unsigned char* iocpbuff = new unsigned char[MAXQUEUE_BUFFER_LEN];
	memset(iocpbuff,0,MAXQUEUE_BUFFER_LEN);
	memcpy_s(iocpbuff,MAXQUEUE_BUFFER_LEN,pIoContext->m_wsaBuf.buf,MAXQUEUE_BUFFER_LEN);
	//EnterCriticalSection(&m_csProcess);
	//while(processList.size()>MAXQUEUE_NUM/10)
	//{
	//	list<unsigned char*>::iterator iterlist = processList.begin();
	//	unsigned char* it = *iterlist;
	//	processList.pop_front();
	//	if(it){
	//		delete it;
	//		it = NULL;
	//	}
	//}
	//processList.push_back(iocpbuff);

	//界面显示
	_SQLformatTime(local_time);
	memcpy(addr,iocpbuff+1,4);
	addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);
	sprintf_s(reade,sizeof(reade),"%u",addrHex);
	//sprintf_s(buff,sizeof(buff),_T("收到  %s:%d 数据包存在粘包 阅读器:%u"),inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),addrHex);
	//sprintf_s(buff,sizeof(buff),_T("收到 阅读器:%u 一数据包存在粘包 "),addrHex);
	//do
	//{
	//	CGsguard guardlock(g_seclog);
	//	this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
	//}while(0);


	//LeaveCriticalSection(&m_csProcess);

	CString strUI = "心跳数据上传";
	if(iocpbuff[5]>=250 || ( iocpbuff[5]<250 && _Check_Sum(iocpbuff,iocpbuff[5] + 5) != iocpbuff[iocpbuff[5] + 5]))
	//if(iocpbuff[5]>=250 )
	{
		return _PostRecv( pIoContext );
	}

	switch(iocpbuff[0])
	{
	case 0x0C:
		do{
			
			if(pIoContext->m_wsaBuf.buf[6] == 0x01)
				strUI = "考勤数据上传";
			else if(pIoContext->m_wsaBuf.buf[6] == 0x02)
				strUI = "定位数据上传";
			if(GetTickCount()- lasttime  >1000){
			   CGsguard guardlock(g_secheart);
			   list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),strUI,CString(local_time));
			   lasttime = GetTickCount();
			}

//////----------------------------
#if 0
			long lasttime = GetTickCount();//for test
			long lasttime1= GetTickCount();
			char b1[64]={0};//for test
			unsigned char type;
			Record_list pm_record_list;
			Record_t  mRecord11[50] = {0};
			int index = 0;
			while(iocpbuff[index] == 0x0C && index<MAXQUEUE_BUFFER_LEN)
			{
				lasttime1 = GetTickCount();//for test
				int statues = _GetRecordList((unsigned char*)(iocpbuff+index), addr, &type, &pm_record_list);
				//for test
				memset(b1,64,0);
				sprintf_s(b1,sizeof(b1),_T("每条数据耗时1 %d微妙"),GetTickCount()-lasttime1);
				if(GetTickCount()-lasttime1 > 1)
					this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
				//end for test

				if (0 != statues)
				{
					addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);
					sprintf_s(buff,sizeof(buff),_T("阅读器:%u 数据包校验失败不解析协议"),addrHex);
					this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
					// 然后开始投递下一个WSARecv请求
					goto end;
				}
				addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);

				//sprintf_s(buff,sizeof(buff),_T("收到  %s:%d 数据信息 阅读器:%u"),inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),addrHex);
				//this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);

				lasttime1 = GetTickCount();//for test
				for (int Si=0; Si<pm_record_list.bCnt && Si<50; Si++)
				{
					mRecord11[Si].bTagType = pm_record_list.mRecord[Si].bTagType;
					memcpy(mRecord11[Si].aTagId, pm_record_list.mRecord[Si].aTagId, 4);
					mRecord11[Si].bCheckSum = pm_record_list.mRecord[Si].bCheckSum;
					memcpy(mRecord11[Si].aRsv, pm_record_list.mRecord[Si].aRsv, 4);
					memcpy(mRecord11[Si].bcdTime, pm_record_list.mRecord[Si].bcdTime,6);
				}

				//for test
				memset(b1,64,0);
				sprintf_s(b1,sizeof(b1),_T("每条数据耗时2 %d微妙"),GetTickCount()-lasttime1);
				if(GetTickCount()-lasttime1 > 1)
					this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
				//end for test

				//考勤
				if (0x01 == type)
				{
					lasttime1 = GetTickCount();//for test
					//1 遍历所有考勤记录
					CString msg1 = "";
					msg1.Append("[");
					for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
					{
						unsigned char sbody[512]={0};
						unsigned char device_type = 0;//考勤不需要这参数

						_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);

						CString submsg = (char*)sbody;
						msg1.Append(submsg);
						//the last item
						if(Ti != pm_record_list.bCnt-1 )
						{
							msg1.Append(",");
						}
					}//end for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)

					//for test
					memset(b1,64,0);
					sprintf_s(b1,sizeof(b1),_T("每条数据耗时3 %d微妙"),GetTickCount()-lasttime1);
					if(GetTickCount()-lasttime1 > 1)
						this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
					//end for test

					msg1.Append("]");
					lasttime1 = GetTickCount();//for test
					_SendMsmq("",msg1);
					//for test
					memset(b1,64,0);
					sprintf_s(b1,sizeof(b1),_T("每条数据耗时4 %d微妙"),GetTickCount()-lasttime1);
					if(GetTickCount()-lasttime1 > 1)
						this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
					//end for test
				}//end if (0x01 == type) //add by ljj 20171020

				//定位数据类型 0x01 == type || 
				else if (0x02 == type)
				{							
					CString msg1 = "";
					msg1.Append("[");
					lasttime1 = GetTickCount();//for test
					for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
					{
						unsigned char sbody[512]={0};
						unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
						_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);	
						CString submsg = (char*)sbody;
						msg1.Append(submsg);
						//the last item
						if(Ti != pm_record_list.bCnt-1 )
						{
							msg1.Append(",");
						}
					}
					//for test
					memset(b1,64,0);
					sprintf_s(b1,sizeof(b1),_T("每条数据耗时3 %d微妙"),GetTickCount()-lasttime1);
					if(GetTickCount()-lasttime1 > 1)
						this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
					//end for test
					msg1.Append("]");
					lasttime1 = GetTickCount();//for test
					_SendMsmq("",msg1);
					//for test
					memset(b1,64,0);
					sprintf_s(b1,sizeof(b1),_T("每条数据耗时4 %d微妙"),GetTickCount()-lasttime1);
					if(GetTickCount()-lasttime1 > 1)
						this->a.WriteLog(b1, DEFTOOLS::NORMAL_L);
					//end for test
					//list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),CString("定位数据上传"),CString(addtime));
				}
				index = index+iocpbuff[index+5]+6;
			}
#endif
//////---------------------------------
		   }while(0);
		break;
	case 0x0D:
		int index = 0;
		do{
			CGsguard guardlock(g_secheart);
			DWORD dwTick = GetTickCount();
			mapfilter_heart::iterator it = g_mapheart_time.find(addrHex);

			if( g_mapheart_time.end() == it )
			{
				tag_heart tgHeartTime;
				tgHeartTime.dwLastread = 0;
				tgHeartTime.dwCurTime = dwTick;
				tgHeartTime.s_ip = CString(inet_ntoa(ClientAddr->sin_addr));
				//tgHeartTime.socketaddr = ClientAddr;
				g_mapheart_time[addrHex] = tgHeartTime;
			}
			else
			{
				//g_mapheart_time[addrHex].socketaddr = ClientAddr;
				g_mapheart_time[addrHex].s_ip = CString(inet_ntoa(ClientAddr->sin_addr));
				g_mapheart_time[addrHex].dwCurTime = dwTick;
			}
			if(GetTickCount()- lasttime  >1000){
				list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),strUI,CString(local_time));
				lasttime = GetTickCount();
			}
//////////////----------------
#if 0
			while(iocpbuff[index] == 0x0D && index<MAXQUEUE_BUFFER_LEN)
			{

			string pIP = g_mapheart_time[addrHex].s_ip;
			//if(pIP.length() > 20){
				unsigned char type = 3;//心跳
				unsigned char sbody[512]={0};
				_HttpPostStr(addrHex,NULL,&type,(unsigned char *)pIP.c_str(),sbody);
				CString msg1 = "";
				msg1.Append("[");
				CString submsg = (char*)sbody;
				msg1.Append(submsg);
				msg1.Append("]");
				_SendMsmq("",msg1);
			//}

				index = index+iocpbuff[index+5]+6;
			}
#endif
///////////----------------		
		}while(0);

		break;
	}
	_Process_data(iocpbuff,pIoContext->m_szMqBuf);
end:
	if(iocpbuff)
	{
		delete(iocpbuff);
		iocpbuff = NULL;
	}
	return _PostRecv( pIoContext );
}

/////////////////////////////////////////////////////
// 将句柄(Socket)绑定到完成端口中
bool CIOCPModel::_AssociateWithIOCP( PER_SOCKET_CONTEXT *pContext )
{
	// 将用于和客户端通信的SOCKET绑定到完成端口中
	HANDLE hTemp = CreateIoCompletionPort((HANDLE)pContext->m_Socket, m_hIOCompletionPort, (DWORD)pContext, 0);

	if (NULL == hTemp)
	{
		this->_ShowMessage(_T("执行CreateIoCompletionPort()出现错误.错误代码：%d"),GetLastError());
		return false;
	}

	return true;
}




//====================================================================================
//
//				    ContextList 相关操作
//
//====================================================================================


//////////////////////////////////////////////////////////////
// 将客户端的相关信息存储到数组中
void CIOCPModel::_AddToContextList( PER_SOCKET_CONTEXT *pHandleData )
{
	EnterCriticalSection(&m_csContextList);

	m_arrayClientContext.Add(pHandleData);	

	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
//	移除某个特定的Context
void CIOCPModel::_RemoveContext( PER_SOCKET_CONTEXT *pSocketContext )
{
	EnterCriticalSection(&m_csContextList);

	for( int i=0;i<m_arrayClientContext.GetCount();i++ )
	{
		if( pSocketContext==m_arrayClientContext.GetAt(i) )
		{
			RELEASE( pSocketContext );			
			m_arrayClientContext.RemoveAt(i);			
			break;
		}
	}

	LeaveCriticalSection(&m_csContextList);
}

////////////////////////////////////////////////////////////////
// 清空客户端信息
void CIOCPModel::_ClearContextList()
{
	EnterCriticalSection(&m_csContextList);

	for( int i=0;i<m_arrayClientContext.GetCount();i++ )
	{
		delete m_arrayClientContext.GetAt(i);
	}

	m_arrayClientContext.RemoveAll();

	LeaveCriticalSection(&m_csContextList);
}



//====================================================================================
//
//				       其他辅助函数定义
//
//====================================================================================



////////////////////////////////////////////////////////////////////
// 获得本机的IP地址
CString CIOCPModel::GetLocalIP()
{
	// 获得本机主机名
	char hostname[MAX_PATH] = {0};
	gethostname(hostname,MAX_PATH);                
	struct hostent FAR* lpHostEnt = gethostbyname(hostname);
	if(lpHostEnt == NULL)
	{
		return DEFAULT_IP;
	}

	// 取得IP地址列表中的第一个为返回的IP(因为一台主机可能会绑定多个IP)
	LPSTR lpAddr = lpHostEnt->h_addr_list[0];      

	// 将IP地址转化成字符串形式
	struct in_addr inAddr;
	memmove(&inAddr,lpAddr,4);
	m_strIP = CString( inet_ntoa(inAddr) );        

	return m_strIP;
}

///////////////////////////////////////////////////////////////////
// 获得本机中处理器的数量
int CIOCPModel::_GetNoOfProcessors()
{
	SYSTEM_INFO si;

	GetSystemInfo(&si);

	return si.dwNumberOfProcessors;
}

/////////////////////////////////////////////////////////////////////
// 在主界面中显示提示信息
void CIOCPModel::_ShowMessage(const CString szFormat,...) const
{
	// 根据传入的参数格式化字符串
	CString   strMessage;
	va_list   arglist;

	// 处理变长参数
	va_start(arglist, szFormat);
	strMessage.FormatV(szFormat,arglist);
	va_end(arglist);

	// 在主界面中显示
	CMainDlg* pMain = (CMainDlg*)m_pMain;
	if( m_pMain!=NULL )
	{
		pMain->AddInformation(strMessage);
		//TRACE( strMessage+_T("\n") );
	}	
}

// 解析数据
int CIOCPModel::_GetRecordList(unsigned char *pRspPkt, unsigned char *bReaderAddr, unsigned char *bReaderType, Record_list *pRecordList)
{
	unsigned char letter[sizeof(Comm_Pkt_Format_t)] = {0};// {0x0b, 0xff, 0x02, 0x00, 0xf4};
	Comm_Pkt_Format_t *pRxPkt = (Comm_Pkt_Format_t *)letter;
	Record_list *pm_record_list = (Record_list *)(pRxPkt->aPara);

	//	BYTE cc = 0;

	memcpy_s(letter,sizeof(Comm_Pkt_Format_t),pRspPkt, pRspPkt[5] + 6);
	//memcpy(letter, pRspPkt, pRspPkt[5] + 6);

	if( pRxPkt->bSOF != 0x0C )
		return -1;
#if 0
	if( _Check_Sum(letter, pRxPkt->bPktLen+5 ) != pRxPkt->aPara[pRxPkt->bPktLen-2] )
		return -2;
#endif
	//	if(pRxPkt->bStatus)
	//		return -3;
	//	*bReaderAddr = pRxPkt->bReaderAddr;
	memcpy(bReaderAddr, pRxPkt->bReaderAddr, 4);
	*bReaderType = pRxPkt->bStatus;
	if(pRxPkt->bPktLen < sizeof(Record_t)+1 )
		return -4;
	pRecordList->bCnt = pm_record_list->bCnt;
	for(int n = 0; n < pRecordList->bCnt && n<50; ++n )
	{
		memcpy(pRecordList->mRecord[n].aTagId, pm_record_list->mRecord[n].aTagId, 4);
		memcpy(pRecordList->mRecord[n].bcdTime, pm_record_list->mRecord[n].bcdTime, 6);
	}
	return 0;
}

BYTE CIOCPModel::_Check_Sum(const BYTE * buf, int len)
{
	BYTE r = 0;
	for (int i = 0; i < len; i++)
	{
		r += buf[i];
	}

	r = ~r + 1;

	return r;
}


void CIOCPModel::_GetCurrenttime(char* currtime)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	CString temp;
	temp.Format(_T("%04d%02d%02d%02d%02d%02d%03d"),st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,st.wMilliseconds);
	char *ch = (LPSTR)(LPCTSTR)temp;
	strcpy(currtime, ch);
}

void CIOCPModel::_SQLformatTime(char *currtime)
{
	SYSTEMTIME st;
	GetLocalTime(&st);
	CString temp;
	temp.Format(_T("%04d-%02d-%02d %02d:%02d:%02d"),st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond);
	char *ch = (LPSTR)(LPCTSTR)temp;
	strcpy(currtime, ch);
}

void CIOCPModel::_HttpPostStr(unsigned int ireaderID,Record_t* record,unsigned char *type,unsigned char *inouttype,unsigned char* outbuf)
{
	//阅读器ID
	char sreaderID[256] = {0};
	sprintf_s(sreaderID, "%u", ireaderID);

	////1.进出校设备，2.进班设备
	//char stype[20] = {0};
	//sprintf_s(stype, "%d", 2);

	//手环ID
	unsigned int wristID = 0;
	char sCardID[256] = {0};
	if(record != NULL)
	{
		wristID = ((record->aTagId[0] << 24) + (record->aTagId[1] << 16) + (record->aTagId[2] << 8) + record->aTagId[3]);
		sprintf(sCardID, "%u", wristID);
	}

	//时间
	char sTime[256] = {0};
	if(record != NULL && record->bcdTime[0]!=0XDD && record->bcdTime[0]!=0XAB && record->bcdTime[0]!=0X00)
	{
		sprintf_s(sTime,sizeof(sTime),"20%02x-%02x-%02x %02x:%02x:%02x", record->bcdTime[0], record->bcdTime[1], record->bcdTime[2], record->bcdTime[3], record->bcdTime[4], record->bcdTime[5]);
	}
	else
	{
		_SQLformatTime(sTime);
	}

	//消息类型
	char sbtype[20] = {0};
	sprintf_s(sbtype, "\"%d\"", type[0]);

	char sbody[512]={0};
	//sprintf_s(sbody,"{\"datas\":[{\"scan_rssi\":%d,\"scan_time\":\"%s\",\"tag_id\":\"%s\",\"type\":%s}]}",-70,sTime,sCardID,sbtype);
	if(type[0] == 1)//考勤
	{
		sprintf_s(sbody,"{\"reader_code\":%u,\"wristband_code\":%u,\"post_time\":\"%s\",\"post_type\":%s}",ireaderID,wristID,sTime,sbtype);
	}
	else if(type[0] == 2)//定位
	{
		sprintf_s(sbody,"{\"reader_code\":%u,\"wristband_code\":%u,\"post_time\":\"%s\",\"post_type\":%s,\"device_type\":%u}",ireaderID,wristID,sTime,sbtype,inouttype[0]);
	}
	else if(type[0] == 3)//心跳
	{
		sprintf(sCardID, "%u", ireaderID);
		if(inouttype == NULL)
		{
			sprintf_s(sbody,"{\"reader_code\":%u,\"post_time\":\"%s\",\"post_type\":%s}",ireaderID,sTime,sbtype);
		}
		else
		{
			sprintf_s(sbody,"{\"reader_code\":%u,\"post_time\":\"%s\",\"post_type\":%s,\"ip\":\"%s\"}",ireaderID,sTime,sbtype,inouttype);
		}	
	}
	else
	{
		return;
	}
	memcpy(outbuf,sbody,sizeof(sbody));
}

void CIOCPModel::_SendMsmq(CString labelmsg,CString msg)
{

	mque.SendMQ(DataIP.c_str(),Catalog.c_str(),labelmsg,msg);
	//add by ljj 20171027
	//agsMSMQ mque;
	//if(!mque.SendPrivateQ(DataIP.c_str(),Catalog.c_str(),labelmsg,msg))
	//if(0)
	//{
		//msg = "数据进消息队列失败 body="+msg;
		//do
		//{
		//	CGsguard guardlock(g_seclog);
		//	a.WriteLog(msg.GetBuffer(), DEFTOOLS::NORMAL_L);
		//	msg.ReleaseBuffer();
		//}while(0);

	//}
	//else
	//{
		//msg = "数据进消息队列成功 body="+msg;
		//do
		//{
		//	CGsguard guardlock(g_seclog);
		//	a.WriteLog(msg.GetBuffer(), DEFTOOLS::NORMAL_L);
		//	msg.ReleaseBuffer();
		//}while(0);
	//}
}

void CIOCPModel::_SendMsmq(CString labelmsg,CString msg,unsigned char* msgbuf){
	//char* c1 = new char[256];
	//memset(c1,256,0);
	//strcpy(c1,"[{\"reader_code\":22,\"wristband_code\":1441822,\"post_time\":\"2018-04-03 15:08:10\",\"post_type\":2,\"device_type\":2}]");
	//mque.SendMQ(DataIP.c_str(),Catalog.c_str(),labelmsg,c1);
	do 
	{
		CGsguard guardlock(g_secatttab);
		mque.SendMQ(DataIP.c_str(),Catalog.c_str(),labelmsg,msg,msgbuf);
	} while (0);
	
}

void CIOCPModel::_RecognitionToJson(unsigned char *reader, unsigned char *wristband, unsigned char *time, char *JsonString, unsigned char *type)
{
	int iPosInString = 0;

	//////左起/////
	strcpy(JsonString + iPosInString, "{");
	iPosInString += strlen("{");

	/////reader_code//////
	unsigned int ireaderID = 0;
	ireaderID = (reader[0] << 24) + (reader[1] << 16) + (reader[2] << 8) + reader[3];
	char sreaderID[256] = {0};
	sprintf_s(sreaderID, "\"%u\"", ireaderID);
	strcpy(JsonString + iPosInString, "\"reader_code\":");
	iPosInString += strlen("\"reader_code\":");

	strcpy(JsonString + iPosInString, sreaderID);
	iPosInString += strlen(sreaderID);

	//////wristband_code///////
	int tag_bytes_cnt = TAG_ID_BYTES_CNT;
	unsigned int wristID = 0;
	wristID = ((wristband[0] << 24) + (wristband[1] << 16) + (wristband[2] << 8) + wristband[3]);

	char sCardID[256] = {0};
	sprintf(sCardID, "\"%x\"", wristID);

	strcpy(JsonString + iPosInString, ",\"wristband_code\":");
	iPosInString += strlen(",\"wristband_code\":");

	strcpy(JsonString + iPosInString, sCardID);
	iPosInString += strlen(sCardID);

	//////post_time///////
	char sTime[256] = {0};
	sprintf_s(sTime,sizeof(sTime),"\"20%02x-%02x-%02x %02x:%02x:%02x\"", time[0], time[1], time[2], time[3], time[4], time[5]);

	strcpy(JsonString + iPosInString, ",\"post_time\":");
	iPosInString += strlen(",\"post_time\":");

	strcpy(JsonString + iPosInString, sTime);
	iPosInString += strlen(sTime);

	//////post_type/////

	strcpy(JsonString + iPosInString, ",\"post_type\":");
	iPosInString += strlen(",\"post_type\":");

	char stype[20] = {0};
	sprintf_s(stype, "\"%d\"", type[0]);

	strcpy(JsonString + iPosInString, stype);
	iPosInString += strlen(stype);

	//////右结//////
	strcpy(JsonString + iPosInString, "}");
	iPosInString += strlen("}");

	//	iPosInString -= 1;
	JsonString[iPosInString] = 0;
}

void CIOCPModel::_GetSeverTimeTOJson(int devicetype, unsigned char *device, unsigned char *time, char *JsonString)
{
	int iPosInString = 0;

	//////左起/////
	strcpy(JsonString + iPosInString, "{");
	iPosInString += strlen("{");

	//////device_type/////
	strcpy(JsonString + iPosInString, "\"device_type\":");
	iPosInString += strlen("\"device_type\":");

	char stype[20] = {0};
	sprintf_s(stype, "\"%d\"", devicetype);

	strcpy(JsonString + iPosInString, stype);
	iPosInString += strlen(stype);

	//////device_code///////
	unsigned int ireaderID = 0;
	ireaderID = ((device[0] << 24) + (device[1] << 16) + (device[2] << 8) + device[3]);
	char sreaderID[256] = {0};
	sprintf_s(sreaderID, "\"%u\"", ireaderID);
	strcpy(JsonString + iPosInString, ",\"device_code\":");
	iPosInString += strlen(",\"device_code\":");

	strcpy(JsonString + iPosInString, sreaderID);
	iPosInString += strlen(sreaderID);

	//////device_time///////
	char sTime[256] = {0};
	sprintf(sTime,"\"20%02x-%02x-%02x %02x:%02x:%02x\"", time[0], time[1], time[2], time[3], time[4], time[5]);

	strcpy(JsonString + iPosInString, ",\"device_time\":");
	iPosInString += strlen(",\"device_time\":");

	strcpy(JsonString + iPosInString, sTime);
	iPosInString += strlen(sTime);

	//////右结//////
	strcpy(JsonString + iPosInString, "}");
	iPosInString += strlen("}");

	//	iPosInString -= 1;
	JsonString[iPosInString] = 0;
}

void CIOCPModel::removeAll(string &str,char c)
{
	string::iterator new_end = remove_if(str.begin(), str.end(), bind2nd(equal_to<char>(),c));
	str.erase(new_end, str.end());
}

void CIOCPModel::Ascii2Hex(unsigned char *hexBuf, const char *ascBuf, unsigned char hexbufLen)
{
	unsigned char i,index;
	unsigned char temp;

	index = 0;
	for(i = 0;i < hexbufLen; i++)
	{
		if((ascBuf[index] <= 0x39)&&(ascBuf[index] >= 0x30))
		{
			temp = ascBuf[index] - 0x30;
		}
		else if((ascBuf[index] <= 'F')&&(ascBuf[index] >= 'A'))
			temp = ascBuf[index] - 0x37;
		else
			temp = 0;

		hexBuf[i] = temp<<4;

		index++;

		if((ascBuf[index] <= 0x39)&&(ascBuf[index] >= 0x30))
		{
			temp = ascBuf[index] - 0x30;
		}
		else if((ascBuf[index] <= 'F')&&(ascBuf[index] >= 'A'))
			temp = ascBuf[index] - 0x37;

		hexBuf[i] += temp;
		index++;
	}	
}
int  CIOCPModel::DectoBCD(int Dec, unsigned char *Bcd, int len)  
{  
	int i;  
	int temp;  
	for (i = len - 1; i >= 0; i--)  
	{  
		temp = Dec % 100;  
		Bcd[i] = ((temp / 10) << 4) + ((temp % 10) & 0x0F);  
		Dec /= 100;  
	}  
	return 0;  
}

/////////////////////////////////////////////////////////////////////
// 判断客户端Socket是否已经断开，否则在一个无效的Socket上投递WSARecv操作会出现异常
// 使用的方法是尝试向这个socket发送数据，判断这个socket调用的返回值
// 因为如果客户端网络异常断开(例如客户端崩溃或者拔掉网线等)的时候，服务器端是无法收到客户端断开的通知的

bool CIOCPModel::_IsSocketAlive(SOCKET s)
{
	int nByteSent=send(s,"",0,0);
	if (-1 == nByteSent) return false;
	return true;
}

///////////////////////////////////////////////////////////////////
// 显示并处理完成端口上的错误
bool CIOCPModel::HandleError( PER_SOCKET_CONTEXT *pContext,const DWORD& dwErr )
{
	// 如果是超时了，就再继续等吧  
	if(WAIT_TIMEOUT == dwErr)  
	{  	
		// 确认客户端是否还活着...
		if( !_IsSocketAlive( pContext->m_Socket) )
		{
			//this->_ShowMessage( _T("检测到客户端1:%s : %d异常退出！",inet_ntoa(pContext->m_ClientAddr.sin_addr), ntohs(pContext->m_ClientAddr.sin_port)) );
			this->_ShowMessage( _T("检测到客户端1:%d : %d异常退出！",pContext->m_Socket, ntohs(pContext->m_ClientAddr.sin_port)) );
			this->_RemoveContext( pContext );
			return false;
		}
		else
		{
			this->_ShowMessage( _T("网络操作超时！重试中...") );
			return true;
		}
	}  

	// 可能是客户端异常退出了
	else if( ERROR_NETNAME_DELETED==dwErr )
	{
		//this->_ShowMessage(  _T("检测到客户端2:%s : %d异常退出！",inet_ntoa(pContext->m_ClientAddr.sin_addr), ntohs(pContext->m_ClientAddr.sin_port)) );
		this->_ShowMessage( _T("检测到客户端2:%d : %d异常退出！",pContext->m_Socket, ntohs(pContext->m_ClientAddr.sin_port)) );
		this->_RemoveContext( pContext );
		return false;
	}

	else
	{
		this->_ShowMessage( _T("完成端口操作出现错误，线程退出。错误代码：%d"),dwErr );
		return false;
		//return true;		//不让线程退出
	}
}

void CIOCPModel::_Gettimerecord()
{
	char szPath[255];
	//获取应用程序完全路径
	::GetModuleFileName(NULL,LPCH(szPath),255);
	CString strFileName = LPCH(szPath);
	//获取所在的目录名称
	strFileName.Delete(strFileName.ReverseFind('\\')+1,strFileName.GetLength ()-strFileName.ReverseFind('\\')-1);
	//构造配置文件的完全路径
	strFileName += "Config.ini";
	TCHAR sz[101] = {0};
	//获取配置文件中的数据
	GetPrivateProfileString(_T("General"),_T(" URL"),_T(""),sz,100,strFileName);
	string url(sz);
	URL = url;
	GetPrivateProfileString(_T("General"),_T(" APPKEY"),_T(""),sz,100,strFileName);
	string appkey(sz);
	AppKey = appkey;
	GetPrivateProfileString(_T("General"),_T(" Data_Source"),_T(""),sz,100,strFileName);
	DataIP = sz;
	GetPrivateProfileString(_T("General"),_T(" Initial_Catalog"),_T(""),sz,100,strFileName);
	Catalog = sz;
	GetPrivateProfileString(_T("General"),_T(" User_ID"),_T(""),sz,100,strFileName);
	UserID = sz;
	GetPrivateProfileString(_T("General"),_T(" Password"),_T(""),sz,100,strFileName);
	Password = sz;
	GetPrivateProfileString(_T("General"),_T("SESSSION"),_T(""),sz,100,strFileName);
	str_sesssion = sz;

	GetPrivateProfileString(_T("General"),_T(" 起始时间1"),_T(""),sz,100,strFileName);
	string begin1(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间1"),_T(""),sz,100,strFileName);
	string end1(sz);
	GetPrivateProfileString(_T("General"),_T(" 起始时间2"),_T(""),sz,100,strFileName);
	string begin2(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间2"),_T(""),sz,100,strFileName);
	string end2(sz);
	GetPrivateProfileString(_T("General"),_T(" 起始时间3"),_T(""),sz,100,strFileName);
	string begin3(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间3"),_T(""),sz,100,strFileName);
	string end3(sz);
	GetPrivateProfileString(_T("General"),_T(" 起始时间4"),_T(""),sz,100,strFileName);
	string begin4(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间4"),_T(""),sz,100,strFileName);
	string end4(sz);
	GetPrivateProfileString(_T("General"),_T(" 起始时间5"),_T(""),sz,100,strFileName);
	string begin5(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间5"),_T(""),sz,100,strFileName);
	string end5(sz);
	GetPrivateProfileString(_T("General"),_T(" 起始时间6"),_T(""),sz,100,strFileName);
	string begin6(sz);
	GetPrivateProfileString(_T("General"),_T(" 结束时间6"),_T(""),sz,100,strFileName);
	string end6(sz);
	if (begin1 != "" && end1 != "")
	{
		TimeMap.insert(pair<string, string>(begin1, end1));
	}
	if (begin2 != "" && end2 != "")
	{
		TimeMap.insert(pair<string, string>(begin2, end2));
	}
	if (begin3 != "" && end3 != "")
	{
		TimeMap.insert(pair<string, string>(begin3, end3));
	}
	if (begin4 != "" && end4 != "")
	{
		TimeMap.insert(pair<string, string>(begin4, end4));
	}
	if (begin5 != "" && end5 != "")
	{
		TimeMap.insert(pair<string, string>(begin5, end5));
	}
	if (begin6 != "" && end6 != "")
	{
		TimeMap.insert(pair<string, string>(begin6, end6));
	}
}

void CIOCPModel::_InitSQL()
{
	char sz[100] = {0};
	char szPath[255] = {0};
	//获取应用程序完全路径
	::GetModuleFileName(NULL,LPCH(szPath),255);
	CString strFileName = LPCH(szPath);
	//获取所在的目录名称
	strFileName.Delete(strFileName.ReverseFind('\\')+1,strFileName.GetLength ()-strFileName.ReverseFind('\\')-1);
	//构造配置文件的完全路径
	strFileName += "Config.ini";
	GetPrivateProfileString("General","nThreshold","1",sz,100,strFileName);
	g_nThres = atoi(sz);

	memset(sz,0,sizeof(sz));
	GetPrivateProfileString("General","nTimeOut","1",sz,100,strFileName);
	g_nTimeout = atoi(sz);

	//port
	memset(sz,0,sizeof(sz));
	GetPrivateProfileString("General","port","12345",sz,100,strFileName);
	m_nPort = atoi(sz);

	////autorun
	//memset(sz,0,sizeof(sz));
	//GetPrivateProfileString("General","autorun",0,sz,100,strFileName);
	//autorun = atoi(sz);

	//wlogflag
	memset(sz,0,sizeof(sz));
	GetPrivateProfileString("General","wlogflag",0,sz,100,strFileName);
	wlogflag = atoi(sz);

	//	::CoUninitialize();//反初始化COM库
}

void CIOCPModel::_SaveConfig()
{
	char sz[100] = {0};
	char szPath[255] = {0};
	//获取应用程序完全路径
	::GetModuleFileName(NULL,LPCH(szPath),255);
	CString strFileName = LPCH(szPath);
	//获取所在的目录名称
	strFileName.Delete(strFileName.ReverseFind('\\')+1,strFileName.GetLength ()-strFileName.ReverseFind('\\')-1);
	//构造配置文件的完全路径
	strFileName += "Config.ini";

	//port
	sprintf_s(sz,"%d",m_nPort);
	WritePrivateProfileString("General","port",sz,strFileName);

	//wlogflag
	memset(sz,0,100);
	sprintf_s(sz,"%d",wlogflag);
	WritePrivateProfileString("General","wlogflag",sz,strFileName);
}


DWORD WINAPI CIOCPModel::_ListenThread(LPVOID lpParam)
{
	CIOCPModel* m_pSerial=(CIOCPModel*)lpParam;
	Sleep(2000);
	m_pSerial->_ShowMessage(_T("监控线程启动"));
	//while (true)
	//{
	//	m_pSerial->_ComareHeart_beat();
	//	Sleep(5000);
	//}
	RELEASE(lpParam);
	return 0;
}

time_t CIOCPModel::StringToDatetime(char *str)  
{  
	tm tm_;  
	int year, month, day, hour, minute,second;  
	sscanf(str,"%d/%d/%d %d:%d:%d.%d", &year, &month, &day, &hour, &minute, &second);  
	tm_.tm_year  = year - 1900;  
	tm_.tm_mon   = month - 1;  
	tm_.tm_mday  = day;  
	tm_.tm_hour  = hour;  
	tm_.tm_min   = minute;  
	tm_.tm_sec   = second;  
	tm_.tm_isdst = 0;  

	time_t t_ = mktime(&tm_); //已经减了8个时区  
	return t_; //秒时间  
}


bool CIOCPModel::_CompareTime()
{
	map<string, string>::iterator it;
	for (it = TimeMap.begin(); it != TimeMap.end(); it++)
	{
		const char *ptime = it->first.c_str();
		const char *pendtime = it->second.c_str();
		int bhour = (ptime[0] - 48)* 10 + (ptime[1] - 48);
		int bmin = (ptime[3] - 48) * 10 + (ptime[4] - 48);
		int bsec = (ptime[6] - 48) * 10 + (ptime[7] - 48);
		int ehour = (pendtime[0] - 48) * 10 + (pendtime[1] - 48);
		int emin = (pendtime[3] - 48) * 10 + (pendtime[4] - 48);
		int esec = (pendtime[6] - 48) * 10 + (pendtime[7] - 48);
		time_t curtime = time(NULL);
		if (GetCurHourByMin(curtime, bhour, bmin, ehour, emin))
		{
			return true;
		}
	}
	return false;
}

bool CIOCPModel::GetCurHourByMin(time_t curTm, int BegHour, int BegMin, int EndHour, int EndMin)
{	
	struct tm *ptm;
	ptm = localtime(&curTm);  //获取当前系统时间 
	if (ptm == NULL )
	{
		//	ErrorLn(ModuleNone, _GT("[CTongZhanActive::GetCurHourByMin] 获取本地时间转换失败"));
		return false;
	}
	ptm->tm_hour = BegHour;
	ptm->tm_min = BegMin;
	DWORD  BegTime = (DWORD)mktime(ptm);  //取得开始时间

	ptm->tm_hour = EndHour;
	ptm->tm_min = EndMin;
	DWORD  Endime = (DWORD)mktime(ptm);    //取得结束时间点

	if (curTm >= BegTime &&  curTm <= Endime)
		return true;

	return false; 
}

string CIOCPModel::ToHex(unsigned long int iNum)
{
	char buffer[100] = {0};
	sprintf_s(buffer,"%x",iNum);

	return string(buffer);
}

DWORD WINAPI CIOCPModel::_ProcessThread(LPVOID lpParam)
{
	CIOCPModel* m_pSerial=(CIOCPModel*)lpParam;
	m_pSerial->_ShowMessage(_T("数据处理线程启动"));
	m_pSerial->_Process_data();

	return 0;
}

#if 0
//void CIOCPModel::_Process_data()
//{
//	while (g_bExtiThread)
//	{
//
//#if 1
//		EnterCriticalSection(&m_csProcess);
//		if(processList.empty())
//		{
//			LeaveCriticalSection(&m_csProcess);
//			Sleep(20);
//			continue;
//		}
//		std::list<unsigned char*>::iterator iterlist = processList.begin();
//		unsigned char* iocpbuf = *iterlist;
//		processList.pop_front();
//		LeaveCriticalSection(&m_csProcess);
//		if(iocpbuf == NULL)
//		{
//			continue;
//		}
//
//		unsigned char addr[4] = {0};
//		unsigned int addrHex;
//		unsigned char type;
//		char buffer[4096] ={0};
//		char httprespond[1024 * 8] = {0};
//		char heart_time[100] = {20};
//		char timer[100];
//		memset(timer, 0 , 100);
//		_GetCurrenttime(timer);
//		string Interception = timer;
//		string checkTime = timer;
//		Interception = Interception.substr(8,4);
//		checkTime = checkTime.substr(8,3);
//		string key = KEY;
//
//		Record_list pm_record_list;
//		Record_t  mRecord11[50] = {0};
//
//		char buff[256] = {0};
//		char sSql[512] = {0};
//		CMainDlg *list_box = (CMainDlg *)m_pMain;
//#endif
//		try{
//			switch(iocpbuf[0])
//			{
//			case 0x0C:
//				{
//					int index = 0;
//					while(iocpbuf[index] == 0x0C && index<255)
//					{
//#if 1
//						int statues = _GetRecordList((unsigned char*)(iocpbuf+index), addr, &type, &pm_record_list);
//						if (0 != statues)
//						{
//							// 然后开始投递下一个WSARecv请求
//							goto end;
//						}
//						addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);
//
//						//sprintf_s(buff,sizeof(buff),_T("收到  %s:%d 数据信息 阅读器:%u"),inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),addrHex);
//						//this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
//
//						for (int Si=0; Si<pm_record_list.bCnt && Si<50; Si++)
//						{
//							mRecord11[Si].bTagType = pm_record_list.mRecord[Si].bTagType;
//							memcpy(mRecord11[Si].aTagId, pm_record_list.mRecord[Si].aTagId, 4);
//							mRecord11[Si].bCheckSum = pm_record_list.mRecord[Si].bCheckSum;
//							memcpy(mRecord11[Si].aRsv, pm_record_list.mRecord[Si].aRsv, 4);
//							memcpy(mRecord11[Si].bcdTime, pm_record_list.mRecord[Si].bcdTime,6);
//						}
//						//考勤
//
//						if (0x01 == type)
//						{
//
//							char reade[32] = {0};
//							char local_time[50];
//							_SQLformatTime(local_time);
//							//1 遍历所有考勤记录
//							for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
//							{
//again:
//#if 1
//								if(Ti >= pm_record_list.bCnt )
//								{
//									break;
//								}
//								unsigned int ring_id;
//								ring_id = ((mRecord11[Ti].aTagId[0] << 24) + (mRecord11[Ti].aTagId[1] << 16) + (mRecord11[Ti].aTagId[2] << 8) + mRecord11[Ti].aTagId[3]);
//
//								bool timeflag = _CompareTime();
//								//2  timeflag
//								if ( timeflag )
//								{
//									////间隔10s上传数据
//									do
//									{
//										//此处用临界区防止g_mapringid2time全局变量被多个线程同时使用
//										CGsguard guardlock(g_section);
//										unsigned char* bcdtime = mRecord11[Ti].bcdTime;
//										DWORD dwTick = (DWORD)((bcdtime[0]*365+bcdtime[1]*30+bcdtime[2])*24*60*60+bcdtime[3]*60*60+bcdtime[4]*60+bcdtime[5]);//GetTickCount();
//
//										mapringID2Time::iterator it = g_mapringID2Time.find(ring_id);
//
//										if( g_mapringID2Time.end() == it )
//										{
//											tagTime tgTime;
//											tgTime.nTime_1 = tgTime.nTime_2;
//											tgTime.nTime_2 = dwTick;
//											g_mapringID2Time[ring_id] = tgTime;
//										}
//										else
//										{
//
//											//if( dwTick - g_mapringID2Time[ring_id].nTime_2 < g_nThres * 1000 )
//											if( dwTick - g_mapringID2Time[ring_id].nTime_2 < g_nThres )
//											{
//												g_mapringID2Time[ring_id].nTime_1 = g_mapringID2Time[ring_id].nTime_2;
//												g_mapringID2Time[ring_id].nTime_2 = dwTick;
//												Ti++;
//												goto again;
//											}
//
//											g_mapringID2Time[ring_id].nTime_1 = g_mapringID2Time[ring_id].nTime_2;
//											g_mapringID2Time[ring_id].nTime_2 = dwTick;
//										}
//									}while(0);
//
//									int iValue = CMainDlg::GetClickVal();
//									//考勤类型有数据就post
//									if( CHOOSE_POSTHTTP == iValue )
//									{
//										unsigned char device_type = 0;//考勤不需要这参数
//										_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type);
//									}
//								} //end if ( timeflag )
//#endif
//							}//end for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
//						}//end if (0x01 == type) //add by ljj 20171020
//
//						//定位数据类型 0x01 == type || 
//						else if (0x02 == type)
//						{
//#if 1
//							char addtime[64] = {0};
//							char reade[32] = {0};
//							for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
//							{
//								//CString ringid,readerid;
//								unsigned int wristID = 0;
//								wristID = ((mRecord11[Ti].aTagId[0] << 24) + (mRecord11[Ti].aTagId[1] << 16) + (mRecord11[Ti].aTagId[2] << 8) + mRecord11[Ti].aTagId[3]);
//								//to hex
//								string wristIDHex = ToHex(wristID);
//								char swristID[32]={0};
//								sprintf_s(swristID, "%u",wristID);
//								sprintf_s(reade,sizeof(reade),"%d",addrHex);
//								_SQLformatTime(addtime);
//
//								int iValue = CMainDlg::GetClickVal();
//
//								//1 初始化定位数据
//								mapfilter_time::iterator iterfilter = g_mapfilter_time.find(wristID);
//								if( g_mapfilter_time.end() == iterfilter )
//								{
//									do
//									{
//										CGsguard guard(g_secfiter);
//										tag_reader tgreader;
//										tgreader.readerid = addrHex;
//										tgreader.s_firttime = addtime;
//										tgreader.dwCurTime = GetTickCount();
//										g_mapfilter_time[wristID] = tgreader;
//									}while(0);
//
//
//									if( CHOOSE_POSTHTTP == iValue )
//									{
//										unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
//										_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type);
//									}
//								}
//								else
//								{
//									//2 重复上报数据 在同一个阅读器下,实时更新最后时间
//									if( iterfilter->second.readerid == addrHex )
//									{
//										//实时记录最后被同一个阅读器读到的时间，方便后续交叉读取时过滤
//										do
//										{
//											CGsguard guard(g_secfiter);
//											iterfilter->second.dwLastread	= GetTickCount();
//
//											iterfilter->second.s_lasttime = addtime;
//
//											//表示已出校，再次收到进校信号
//											//2.1 超过5min表示进校
//											if( 0 == iterfilter->second.dwCurTime || 
//												::GetTickCount() - iterfilter->second.dwCurTime > 1000 * 60 * g_nTimeout ) //5min
//											{
//												iterfilter->second.dwCurTime = ::GetTickCount();
//
//												if( CHOOSE_POSTHTTP == iValue )
//												{
//													unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
//													_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type);
//													//if (_SendToHTTP(postinfo, httprespond)) //定位
//													//{
//													//	a.WriteLog("定位数据发送失败");
//													//}
//													//else
//													//{
//													//	a.WriteLog(id +" "+swristID+ "天喻 定位数据 同一阅读器N分钟后进入表示进校 结果:"+httprespond+" post:"+postinfo.s_body);
//													//}
//												}
//											}
//											else
//											{
//												iterfilter->second.dwCurTime = ::GetTickCount();
//											}
//											//}
//										}while(0);
//
//										continue;
//									}
//									else
//									{
//										//2.3 不同阅读器
//										do
//										{
//											CGsguard guard(g_secfiter);
//											iterfilter->second.dwCurTime = 0; //reset zero
//											//2.4 如果上上次阅读器和这次相同,且两者间隔时间小于1min，则过滤掉
//											if( iterfilter->second.last_readerid == addrHex )
//											{
//												if( GetTickCount() - iterfilter->second.dwLastread < 1000 * 60 )	//1min
//												{
//													//ADD BY LJJ FOR TEST 20170512
//													sprintf_s(reade, "%d",addrHex);
//													string id = reade;
//													a.WriteLog(id + " "+swristID+" 定位数据 上上次阅读器和这次相同,且两者间隔时间小于1min，则过滤掉");
//													continue;
//												}
//											}
//
//											//2.4 不同阅读器 上一个阅读器出校  
//											if( CHOOSE_POSTHTTP == iValue )
//											{
//												unsigned char device_type = 4;//1.进校，2出校 3进班 4离班
//												_HttpPostStr(iterfilter->second.readerid,&mRecord11[Ti],&type,&device_type);
//												//if (_SendToHTTP(postinfo, httprespond)) //定位
//												//{
//												//	a.WriteLog("定位数据发送失败");
//												//}
//												//else
//												//{
//												//	string id = reade;
//												//	a.WriteLog(id +" "+swristID+ "天喻 定位数据 不同阅读器 上一个阅读器出校 结果:"+httprespond+" post:"+postinfo.s_body);
//												//}
//
//											}
//
//											//记录当前所在的阅读器id,把上次被读到的阅读器id保存起来
//											iterfilter->second.last_readerid	= iterfilter->second.readerid;
//											iterfilter->second.readerid			= addrHex;
//											iterfilter->second.s_firttime		= addtime;
//											iterfilter->second.dwCurTime = ::GetTickCount();
//
//										}while(0);
//
//										//2.5 不同阅读器表示进另一个班级
//										if( CHOOSE_POSTHTTP == iValue )
//										{
//											unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
//											_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type);
//											//if (_SendToHTTP(postinfo, httprespond)) //定位
//											//{
//											//	a.WriteLog("定位数据发送失败");
//											//}
//											//else
//											//{
//											//	string id = reade;
//											//	a.WriteLog(id +" "+swristID+ "天喻 定位数据 不同阅读器 当前阅读器入校 结果:"+httprespond+" post:"+postinfo.s_body);
//											//}
//
//										}
//									}
//								}
//							}
//
//							//list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),CString("定位数据上传"),CString(addtime));
//#endif
//						}
//#endif
//						index = index+iocpbuf[index+5]+6;
//					}
//					break;
//				}//end case 0x0C:
//			case 0x0D:
//				{
//#if 1
//					unsigned char temp[20] = {0};
//					unsigned char device[20] = {0};
//					unsigned char readertime[20] = {0};
//					char local_time[64] = {0};
//					char reade[32] = {0};
//					int online = 1;
//					int reader_status = 0;
//					_SQLformatTime(local_time);
//					memcpy(temp, iocpbuf,20);
//
//					if( temp[5] < 15 && _Check_Sum(temp,temp[5] + 5) == temp[temp[5] + 5])
//					{
//						addrHex = ((temp[1] << 24) + (temp[2] << 16) + (temp[3] << 8) + temp[4]);
//						//sprintf_s(reade,sizeof(reade),"%u",addrHex);
//						//sprintf_s(buff,sizeof(buff),_T("收到心跳信息 阅读器:%u"),addrHex);
//						//do
//						//{
//						//	CGsguard guardlock(g_seclog);
//						//	this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
//						//}while(0);
//
//						memcpy(device, temp + 1, 4);
//						memcpy(readertime, temp + 7, 6);  // reader上传的时间，暂未使用
//					}//end if
//
//#endif
//
//					//心跳处理逻辑
//#if 1
//					//1判断心跳map是否存在，存在才处理sql
//					do{
//						CGsguard guardlock(g_secheart);
//						DWORD dwTick = GetTickCount();
//						mapfilter_heart::iterator it = g_mapheart_time.find(addrHex);
//
//						if( g_mapheart_time.end() != it )
//						{						
//							{
//								string pIP = g_mapheart_time[addrHex].s_ip;
//								if(GetTickCount() - g_mapheart_time[addrHex].dwLastread > 60*1000)
//								{
//									//sprintf_s(sSql,sizeof(sSql),_T("if EXISTS(SELECT 1 FROM dbo.dodool_heart_beat where readerID = %u)  update dbo.dodool_heart_beat set report_time = '%s',status = 1 where readerID = %u else insert into dbo.dodool_heart_beat(readerID,readerIP,report_time,status) values(%u,'%s','%s',1)"), 
//									//	addrHex,
//									//	local_time, addrHex,
//									//	addrHex,pIP.c_str(),local_time);
//
//									unsigned char type = 3;//心跳
//									_HttpPostStr(addrHex,NULL,&type,NULL);
//									g_mapheart_time[addrHex].dwLastread = dwTick;
//								}
//							}
//							g_mapheart_time[addrHex].dwCurTime = dwTick;
//						}
//					}while(0);
//#endif
//					//LeaveCriticalSection(&m_csSqlList);
//					break;
//				}//end case 0x0C:
//
//			default:
//				break;
//			}//end switch(iocpbuf[0])
//		}
//		catch(...)
//		{
//		}
//end:
//		if(iocpbuf)
//		{
//			delete(iocpbuf);
//			iocpbuf = NULL;
//		}
//	}//end while (g_bExtiThread)
//}//----------end
#endif


void CIOCPModel::_Process_data()
{
	
	while (g_bExtiThread)
	{
#if 1
		EnterCriticalSection(&m_csProcess);
		if(processList.empty())
		{
			LeaveCriticalSection(&m_csProcess);
			Sleep(20);
			continue;
		}
		std::list<unsigned char*>::iterator iterlist = processList.begin();
		unsigned char* iocpbuf = *iterlist;
		processList.pop_front();
		LeaveCriticalSection(&m_csProcess);
		if(iocpbuf == NULL)
		{
			continue;
		}

		unsigned char addr[4] = {0};
		unsigned int addrHex;
		unsigned char type;
		char buffer[4096] ={0};
		char httprespond[1024 * 8] = {0};
		char heart_time[100] = {20};
		char timer[100];
		memset(timer, 0 , 100);
		_GetCurrenttime(timer);
		string Interception = timer;
		string checkTime = timer;
		Interception = Interception.substr(8,4);
		checkTime = checkTime.substr(8,3);
		string key = KEY;

		Record_list pm_record_list;
		Record_t  mRecord11[50] = {0};

		char buff[256] = {0};
		char sSql[512] = {0};
		CMainDlg *list_box = (CMainDlg *)m_pMain;
#endif
		try{
			switch(iocpbuf[0])
			{
			case 0x0C:
				{
					int index = 0;
					while(iocpbuf[index] == 0x0C && index<MAXQUEUE_BUFFER_LEN)
					{
						
						int statues = _GetRecordList((unsigned char*)(iocpbuf+index), addr, &type, &pm_record_list);

						if (0 != statues)
						{
							addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);
							sprintf_s(buff,sizeof(buff),_T("阅读器:%u 数据包校验失败不解析协议"),addrHex);
							this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
							// 然后开始投递下一个WSARecv请求
							goto end;
						}
						addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);

						//sprintf_s(buff,sizeof(buff),_T("收到  %s:%d 数据信息 阅读器:%u"),inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),addrHex);
						//this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
						
						for (int Si=0; Si<pm_record_list.bCnt && Si<50; Si++)
						{
							mRecord11[Si].bTagType = pm_record_list.mRecord[Si].bTagType;
							memcpy(mRecord11[Si].aTagId, pm_record_list.mRecord[Si].aTagId, 4);
							mRecord11[Si].bCheckSum = pm_record_list.mRecord[Si].bCheckSum;
							memcpy(mRecord11[Si].aRsv, pm_record_list.mRecord[Si].aRsv, 4);
							memcpy(mRecord11[Si].bcdTime, pm_record_list.mRecord[Si].bcdTime,6);
						}

						//考勤
						if (0x01 == type)
						{
							//1 遍历所有考勤记录
							CString msg1 = "";
							msg1.Append("[");
							for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
							{
								    unsigned char sbody[512]={0};
									unsigned char device_type = 0;//考勤不需要这参数
									
									_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);
									
									CString submsg = (char*)sbody;
									msg1.Append(submsg);
									//the last item
									if(Ti != pm_record_list.bCnt-1 )
									{
										msg1.Append(",");
									}
							}//end for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)


							msg1.Append("]");
							_SendMsmq("",msg1);
						}//end if (0x01 == type) //add by ljj 20171020

						//定位数据类型 0x01 == type || 
						else if (0x02 == type)
						{							
							CString msg1 = "";
						    msg1.Append("[");
							for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
							{
								unsigned char sbody[512]={0};
								unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
								_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);	
								CString submsg = (char*)sbody;
								msg1.Append(submsg);
								//the last item
								if(Ti != pm_record_list.bCnt-1 )
								{
									msg1.Append(",");
								}
							}
							msg1.Append("]");
							_SendMsmq("",msg1);
							//list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),CString("定位数据上传"),CString(addtime));
						}
						index = index+iocpbuf[index+5]+6;
					}
					break;
				}//end case 0x0C:
			case 0x0D:
				{
					unsigned char temp[20] = {0};
					memcpy(temp, iocpbuf,20);

					if( temp[5] < 15 && _Check_Sum(temp,temp[5] + 5) == temp[temp[5] + 5])
					{
						addrHex = ((temp[1] << 24) + (temp[2] << 16) + (temp[3] << 8) + temp[4]);
						//1判断心跳map是否存在，存在才处理sql
						do{
							CGsguard guardlock(g_secheart);
							mapfilter_heart::iterator it = g_mapheart_time.find(addrHex);
							if( g_mapheart_time.end() != it )
							{						
									string pIP = g_mapheart_time[addrHex].s_ip;
									if(pIP.length() > 20)
										break;
									unsigned char type = 3;//心跳
									unsigned char sbody[512]={0};
									_HttpPostStr(addrHex,NULL,&type,(unsigned char *)pIP.c_str(),sbody);
									CString msg1 = "";
									msg1.Append("[");
									CString submsg = (char*)sbody;
									msg1.Append(submsg);
									msg1.Append("]");
									_SendMsmq("",msg1);
							}
						}while(0);

						//unsigned char type = 3;//心跳
						//_HttpPostStr(addrHex,NULL,&type,NULL);
					}//end if
					break;
				}//end case 0x0C:

			default:
				break;
			}//end switch(iocpbuf[0])
		}
		catch(...)
		{
		}
end:
		if(iocpbuf)
		{
			delete(iocpbuf);
			iocpbuf = NULL;
		}
	}//end while (g_bExtiThread)
}//----------end

void CIOCPModel::_Process_data(unsigned char* iocpbuf,unsigned char* mqbuf)
{
		if(iocpbuf == NULL)
		{
			return;
		}

		unsigned char addr[4] = {0};
		char buff[256] = {0};
		unsigned int addrHex;
		unsigned char type;
		Record_list pm_record_list;
		Record_t  mRecord11[50] = {0};
		//unsigned char	pMsg[2048]={0};
		unsigned char* pMsg = mqbuf;
		//CString msg1 = "";

		int index = 0;

		while((iocpbuf[index] == 0x0C || iocpbuf[index] == 0x0D) && index<MAXQUEUE_BUFFER_LEN)
		{
																																																																																																														try{
			switch(iocpbuf[index])
			{
			case 0x0C:
				{
						int statues = _GetRecordList((unsigned char*)(iocpbuf+index), addr, &type, &pm_record_list);

						if (0 != statues)
						{
							addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);
							sprintf_s(buff,sizeof(buff),_T("阅读器:%u 数据包校验失败不解析协议"),addrHex);
							this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);
							// 然后开始投递下一个WSARecv请求
							goto end;
						}
						addrHex = ((addr[0] << 24) + (addr[1] << 16) + (addr[2] << 8) + addr[3]);

						//sprintf_s(buff,sizeof(buff),_T("收到  %s:%d 数据信息 阅读器:%u"),inet_ntoa(ClientAddr->sin_addr), ntohs(ClientAddr->sin_port),addrHex);
						//this->a.WriteLog(buff, DEFTOOLS::NORMAL_L);

						for (int Si=0; Si<pm_record_list.bCnt && Si<50; Si++)
						{
							mRecord11[Si].bTagType = pm_record_list.mRecord[Si].bTagType;
							memcpy(mRecord11[Si].aTagId, pm_record_list.mRecord[Si].aTagId, 4);
							mRecord11[Si].bCheckSum = pm_record_list.mRecord[Si].bCheckSum;
							memcpy(mRecord11[Si].aRsv, pm_record_list.mRecord[Si].aRsv, 4);
							memcpy(mRecord11[Si].bcdTime, pm_record_list.mRecord[Si].bcdTime,6);
						}

						//考勤
						if (0x01 == type)
						{
							//1 遍历所有考勤记录
							do 
							{
								CString msg1;
								msg1.Empty();
								msg1.Append("[");
								unsigned char sbody[512]={0};
								unsigned char device_type = 0;//考勤不需要这参数
								for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
								{
									_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);
									msg1.Append((char*)sbody);
									//the last item
									if(Ti != pm_record_list.bCnt-1 )
									{
										msg1.Append(",");
									}
								}//end for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)


								msg1.Append("]");
								//unsigned char	pMsg[2048]={0};

								//CGsguard guardlock(g_secatttab);
								memset(pMsg,2048,0);
								//memcpy_s(pMsg,2048,(char*)msg1.GetBuffer(),msg1.GetLength());
								//strncpy((char*)pMsg,msg1,msg1.GetLength());
								memcpy_s(pMsg,2048,msg1.GetBuffer(msg1.GetLength()+1),msg1.GetLength());
								msg1.ReleaseBuffer();
								_SendMsmq("",msg1,pMsg);
							} while (0);
						}//end if (0x01 == type) //add by ljj 20171020

						//定位数据类型 0x01 == type || 
						else if (0x02 == type)
						{
							do 
							{
								//CGsguard guardlock(g_secfiter);
								CString msg1 = "";
								msg1 = "";
								msg1.Append("[");
								for (int Ti = 0; Ti < pm_record_list.bCnt; Ti++)
								{
									unsigned char sbody[512]={0};
									unsigned char device_type = 3;//1.进校，2出校 3进班 4离班
									_HttpPostStr(addrHex,&mRecord11[Ti],&type,&device_type,sbody);	
									CString submsg = (char*)sbody;
									msg1.Append(submsg);
									//the last item
									if(Ti != pm_record_list.bCnt-1 )
									{
										msg1.Append(",");
									}
								}
								msg1.Append("]");
								//unsigned char	pMsg[2048]={0};
							
								memset(pMsg,2048,0);
								//memcpy_s(pMsg,2048,(char*)msg1,msg1.GetLength());
								//strncpy((char*)pMsg,msg1,msg1.GetLength());
								memcpy_s(pMsg,2048,msg1.GetBuffer(msg1.GetLength()+1),msg1.GetLength());
								msg1.ReleaseBuffer();
								_SendMsmq("",msg1,pMsg);
							} while (0);
							
							//list_box->AddDebugInfo(CString(reade),CString(inet_ntoa(ClientAddr->sin_addr)),CString("定位数据上传"),CString(addtime));
						}
					
					break;
				}//end case 0x0C:
			case 0x0D:
				{
					unsigned char temp[20] = {0};
					memcpy(temp, iocpbuf,20);

					if( temp[5] < 15 && _Check_Sum(temp,temp[5] + 5) == temp[temp[5] + 5])
					{
						addrHex = ((temp[1] << 24) + (temp[2] << 16) + (temp[3] << 8) + temp[4]);
						//1判断心跳map是否存在，存在才处理sql
						do{
							CGsguard guardlock(g_secheart);
							mapfilter_heart::iterator it = g_mapheart_time.find(addrHex);
							if( g_mapheart_time.end() != it )
							{						
								string pIP = g_mapheart_time[addrHex].s_ip;
								if(pIP.length() > 20)
									break;
								unsigned char type = 3;//心跳
								char sbody[512]={0};
								_HttpPostStr(addrHex,NULL,&type,(unsigned char *)pIP.c_str(),(unsigned char*)sbody);
								CString msg1 = "";
								msg1 = "";
								msg1.Append("[");
								msg1.Append(sbody);
								msg1.Append("]");
								//unsigned char	pMsg[2048]={0};
								memset(pMsg,2048,0);
								//sprintf_s((char*)pMsg,2048,"[%s]",(char*)sbody);
								//strncpy((char*)pMsg,msg1,msg1.GetLength());
								//memcpy_s(pMsg,2048,msg1.GetBuffer(msg1.GetLength()+1),msg1.GetLength());
								//msg1.ReleaseBuffer();
								sprintf_s((char*)pMsg,2048,_T("[%s]"),sbody);
								_SendMsmq("",msg1,pMsg);
							}
						}while(0);
					}//end if
					break;
				}//end case 0x0C:

			default:
				break;
			}//end switch(iocpbuf[0])
		}
			catch(...)
			{
			}
end:
			index = index+iocpbuf[index+5]+6;
		}

}//----------end