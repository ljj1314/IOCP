


#include "stdafx.h"
#include "PiggyIOCPServer.h"
#include "MainDlg.h"
//#include "AttState.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

int CMainDlg::m_WhichClick = CHOOSE_POSTHTTP;
bool CMainDlg::m_Logflag = false;

// 用于应用程序“关于”菜单项的 CAboutDlg 对话框

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// 对话框数据
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV 支持

// 实现
protected:
	DECLARE_MESSAGE_MAP()
};

CAboutDlg::CAboutDlg() : CDialog(CAboutDlg::IDD)
{
}

void CAboutDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CAboutDlg, CDialog)
END_MESSAGE_MAP()


// CMainDlg 对话框




CMainDlg::CMainDlg(CWnd* pParent /*=NULL*/)
	: CDialog(CMainDlg::IDD, pParent)
{
	m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
}

void CMainDlg::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

BEGIN_MESSAGE_MAP(CMainDlg, CDialog)
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	//}}AFX_MSG_MAP
	ON_BN_CLICKED(IDOK, &CMainDlg::OnBnClickedOk)
	ON_BN_CLICKED(IDC_STOP, &CMainDlg::OnBnClickedStop)
	ON_WM_CLOSE()
	//ON_MESSAGE(WM_MSG_NEW_MSG,OnNewMsg)
	ON_BN_CLICKED(IDCANCEL, &CMainDlg::OnBnClickedCancel)
	ON_WM_DESTROY()
	ON_BN_CLICKED(IDC_BUTTON1, &CMainDlg::OnBnClickedButton1)
	ON_BN_CLICKED(IDC_RADIO_PROCEDURE, &CMainDlg::OnBnClickedRadioProcedure)
	ON_BN_CLICKED(IDC_RADIO_POSTHTTP, &CMainDlg::OnBnClickedRadioPosthttp)
	ON_BN_CLICKED(IDC_RADIO_NONE, &CMainDlg::OnBnClickedRadioNone)
	ON_BN_CLICKED(IDC_CHECK1, &CMainDlg::OnBnClickedCheck1)
END_MESSAGE_MAP()


// CMainDlg 消息处理程序

BOOL CMainDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// 将“关于...”菜单项添加到系统菜单中。

	// IDM_ABOUTBOX 必须在系统命令范围内。
	ASSERT((IDM_ABOUTBOX & 0xFFF0) == IDM_ABOUTBOX);
	ASSERT(IDM_ABOUTBOX < 0xF000);

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL)
	{
		CString strAboutMenu;
		strAboutMenu.LoadString(IDS_ABOUTBOX);
		if (!strAboutMenu.IsEmpty())
		{
			pSysMenu->AppendMenu(MF_SEPARATOR);
			pSysMenu->AppendMenu(MF_STRING, IDM_ABOUTBOX, strAboutMenu);
		}
	}

	// 设置此对话框的图标。当应用程序主窗口不是对话框时，框架将自动
	//  执行此操作
	SetIcon(m_hIcon, TRUE);			// 设置大图标
	SetIcon(m_hIcon, FALSE);		// 设置小图标
	// 初始化界面信息
	HRESULT hr=::CoInitialize(NULL);//初始化Com
	this->Init();	
	m_IOCP._Gettimerecord();
	//初始化配置
	m_IOCP._InitSQL();
	this->InitConfig();

	((CButton *)GetDlgItem(IDC_RADIO_POSTHTTP))->SetCheck(TRUE);

	//自启动
	OnBnClickedOk();
	return TRUE;  // 除非将焦点设置到控件，否则返回 TRUE
}

void CMainDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	if ((nID & 0xFFF0) == IDM_ABOUTBOX)
	{
		CAboutDlg dlgAbout;
		dlgAbout.DoModal();
	}
	else
	{
		CDialog::OnSysCommand(nID, lParam);
	}
}

// 如果向对话框添加最小化按钮，则需要下面的代码
//  来绘制该图标。对于使用文档/视图模型的 MFC 应用程序，
//  这将由框架自动完成。

void CMainDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // 用于绘制的设备上下文

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// 使图标在工作区矩形中居中
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// 绘制图标
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

//当用户拖动最小化窗口时系统调用此函数取得光标
//显示。
HCURSOR CMainDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//////////////////////////////////////////////////////////////////////
// 初始化Socket库以及界面信息
void CMainDlg::Init()
{
	// 初始化Socket库
	if( false==m_IOCP.LoadSocketLib() )
	{
		AfxMessageBox(_T("加载Winsock 2.2失败，服务器端无法运行！"));
		PostQuitMessage(0);
	}

	// 初始化列表
	this->InitListCtrl();
	// 绑定主界面指针(为了方便在界面中显示信息 )
	m_IOCP.SetMainDlg(this);
}

void CMainDlg::InitConfig()
{
	// 设置本机IP地址
	SetDlgItemText( IDC_STATIC_SERVERIP,m_IOCP.GetLocalIP() );
	// 设置默认端口
	SetDlgItemInt( IDC_EDIT_PORT,m_IOCP.m_nPort );

	//日志
	if(m_IOCP.wlogflag == 1)
	{
		m_Logflag = TRUE;
	}
	else
	{
		m_Logflag = FALSE;
	}
	((CButton *)GetDlgItem(IDC_CHECK1))->SetCheck(m_Logflag);
}
///////////////////////////////////////////////////////////////////////
//	开始监听
void CMainDlg::OnBnClickedOk()
{
	SetConfig();
	if( false==m_IOCP.Start() )
	{
		AfxMessageBox(_T("服务器启动失败！"));
		return;	
	}

	GetDlgItem(IDOK)->EnableWindow(FALSE);
	GetDlgItem(IDC_STOP)->EnableWindow(TRUE);
}

//////////////////////////////////////////////////////////////////////
//	结束监听
void CMainDlg::OnBnClickedStop()
{
	SetConfig();
	m_IOCP.Stop();

	GetDlgItem(IDC_STOP)->EnableWindow(FALSE);
	GetDlgItem(IDOK)->EnableWindow(TRUE);

	//清空控件上的显示信息
	CListCtrl *pList = (CListCtrl *)GetDlgItem(IDC_LIST_MSG);
	if( pList )
	{
		pList->DeleteAllItems();
	}
}

// wirte
void CMainDlg::SetConfig(){
	CString sWl(_T(""));
	GetDlgItemText(IDC_EDIT_PORT, sWl);
	int iport = _ttoi(sWl);
	m_IOCP.wlogflag = m_Logflag;
	m_IOCP.SetPort(iport);
	m_IOCP._SaveConfig();
}

///////////////////////////////////////////////////////////////////////
//	初始化List Control
void CMainDlg::InitListCtrl()
{
	CListCtrl* pList = (CListCtrl*)GetDlgItem(IDC_LIST_INFO);
	pList->SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
	pList->InsertColumn(0,_T("服务器调试信息"),LVCFMT_LEFT,500);

	CListCtrl* pListInfo = (CListCtrl*)GetDlgItem(IDC_LIST_MSG);
	pListInfo->SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
	pListInfo->InsertColumn(0,_T("索引"),LVCFMT_LEFT,50);
	pListInfo->InsertColumn(1,_T("ReadID"),LVCFMT_LEFT,150);
	//pListInfo->InsertColumn(2,_T("连接状态"),LVCFMT_LEFT,80);
	pListInfo->InsertColumn(2,_T("IP地址"),LVCFMT_LEFT,150);
	pListInfo->InsertColumn(3,_T("当前状态"),LVCFMT_LEFT,150);		//登录，上传数据...
	pListInfo->InsertColumn(4,_T("上传时间"),LVCFMT_LEFT,150);
}

///////////////////////////////////////////////////////////////////////
//	点击“退出”的时候，停止监听，清空Socket类库
void CMainDlg::OnBnClickedCancel()
{
	// 停止监听
	//m_IOCP.Test();
	SetConfig();
	m_IOCP.Stop();
	CDialog::OnCancel();
}

///////////////////////////////////////////////////////////////////////
//	系统退出的时候，为确保资源释放，停止监听，清空Socket类库
void CMainDlg::OnDestroy()
{
	OnBnClickedCancel();
	::CoUninitialize(); 
	CDialog::OnDestroy();
}

void CMainDlg::OnBnClickedButton1()
{
	// TODO: Add your control notification handler code here
	// 创建对话框实例
	//AttState dlg;
	//// 打开对话框
	//dlg.DoModal();
}

void CMainDlg::OnBnClickedRadioProcedure()
{
	// TODO: 在此添加控件通知处理程序代码
	m_WhichClick = CHOOSE_PROCUDURE;
}

void CMainDlg::OnBnClickedRadioPosthttp()
{
	// TODO: 在此添加控件通知处理程序代码
	m_WhichClick = CHOOSE_POSTHTTP;
}

void CMainDlg::OnBnClickedRadioNone()
{
	// TODO: 在此添加控件通知处理程序代码
	m_WhichClick = CHOOSE_NONE;
}

void CMainDlg::OnBnClickedCheck1()
{
	// TODO: 在此添加控件通知处理程序代码
	int iState = ((CButton *)GetDlgItem(IDC_CHECK1))->GetCheck();
	if( 0 == iState )
	{
		m_Logflag = false;
	}
	else if( 1 == iState )
	{
		m_Logflag = true;
	}
}