


#include "stdafx.h"
#include "PiggyIOCPServer.h"
#include "MainDlg.h"
//#include "AttState.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

int CMainDlg::m_WhichClick = CHOOSE_POSTHTTP;
bool CMainDlg::m_Logflag = false;

// ����Ӧ�ó��򡰹��ڡ��˵���� CAboutDlg �Ի���

class CAboutDlg : public CDialog
{
public:
	CAboutDlg();

// �Ի�������
	enum { IDD = IDD_ABOUTBOX };

	protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV ֧��

// ʵ��
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


// CMainDlg �Ի���




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


// CMainDlg ��Ϣ�������

BOOL CMainDlg::OnInitDialog()
{
	CDialog::OnInitDialog();

	// ��������...���˵�����ӵ�ϵͳ�˵��С�

	// IDM_ABOUTBOX ������ϵͳ���Χ�ڡ�
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

	// ���ô˶Ի����ͼ�ꡣ��Ӧ�ó��������ڲ��ǶԻ���ʱ����ܽ��Զ�
	//  ִ�д˲���
	SetIcon(m_hIcon, TRUE);			// ���ô�ͼ��
	SetIcon(m_hIcon, FALSE);		// ����Сͼ��
	// ��ʼ��������Ϣ
	HRESULT hr=::CoInitialize(NULL);//��ʼ��Com
	this->Init();	
	m_IOCP._Gettimerecord();
	//��ʼ������
	m_IOCP._InitSQL();
	this->InitConfig();

	((CButton *)GetDlgItem(IDC_RADIO_POSTHTTP))->SetCheck(TRUE);

	//������
	OnBnClickedOk();
	return TRUE;  // ���ǽ��������õ��ؼ������򷵻� TRUE
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

// �����Ի��������С����ť������Ҫ����Ĵ���
//  �����Ƹ�ͼ�ꡣ����ʹ���ĵ�/��ͼģ�͵� MFC Ӧ�ó���
//  �⽫�ɿ���Զ���ɡ�

void CMainDlg::OnPaint()
{
	if (IsIconic())
	{
		CPaintDC dc(this); // ���ڻ��Ƶ��豸������

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		// ʹͼ���ڹ����������о���
		int cxIcon = GetSystemMetrics(SM_CXICON);
		int cyIcon = GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		// ����ͼ��
		dc.DrawIcon(x, y, m_hIcon);
	}
	else
	{
		CDialog::OnPaint();
	}
}

//���û��϶���С������ʱϵͳ���ô˺���ȡ�ù��
//��ʾ��
HCURSOR CMainDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

//////////////////////////////////////////////////////////////////////
// ��ʼ��Socket���Լ�������Ϣ
void CMainDlg::Init()
{
	// ��ʼ��Socket��
	if( false==m_IOCP.LoadSocketLib() )
	{
		AfxMessageBox(_T("����Winsock 2.2ʧ�ܣ����������޷����У�"));
		PostQuitMessage(0);
	}

	// ��ʼ���б�
	this->InitListCtrl();
	// ��������ָ��(Ϊ�˷����ڽ�������ʾ��Ϣ )
	m_IOCP.SetMainDlg(this);
}

void CMainDlg::InitConfig()
{
	// ���ñ���IP��ַ
	SetDlgItemText( IDC_STATIC_SERVERIP,m_IOCP.GetLocalIP() );
	// ����Ĭ�϶˿�
	SetDlgItemInt( IDC_EDIT_PORT,m_IOCP.m_nPort );

	//��־
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
//	��ʼ����
void CMainDlg::OnBnClickedOk()
{
	SetConfig();
	if( false==m_IOCP.Start() )
	{
		AfxMessageBox(_T("����������ʧ�ܣ�"));
		return;	
	}

	GetDlgItem(IDOK)->EnableWindow(FALSE);
	GetDlgItem(IDC_STOP)->EnableWindow(TRUE);
}

//////////////////////////////////////////////////////////////////////
//	��������
void CMainDlg::OnBnClickedStop()
{
	SetConfig();
	m_IOCP.Stop();

	GetDlgItem(IDC_STOP)->EnableWindow(FALSE);
	GetDlgItem(IDOK)->EnableWindow(TRUE);

	//��տؼ��ϵ���ʾ��Ϣ
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
//	��ʼ��List Control
void CMainDlg::InitListCtrl()
{
	CListCtrl* pList = (CListCtrl*)GetDlgItem(IDC_LIST_INFO);
	pList->SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
	pList->InsertColumn(0,_T("������������Ϣ"),LVCFMT_LEFT,500);

	CListCtrl* pListInfo = (CListCtrl*)GetDlgItem(IDC_LIST_MSG);
	pListInfo->SetExtendedStyle(LVS_EX_FULLROWSELECT|LVS_EX_GRIDLINES);
	pListInfo->InsertColumn(0,_T("����"),LVCFMT_LEFT,50);
	pListInfo->InsertColumn(1,_T("ReadID"),LVCFMT_LEFT,150);
	//pListInfo->InsertColumn(2,_T("����״̬"),LVCFMT_LEFT,80);
	pListInfo->InsertColumn(2,_T("IP��ַ"),LVCFMT_LEFT,150);
	pListInfo->InsertColumn(3,_T("��ǰ״̬"),LVCFMT_LEFT,150);		//��¼���ϴ�����...
	pListInfo->InsertColumn(4,_T("�ϴ�ʱ��"),LVCFMT_LEFT,150);
}

///////////////////////////////////////////////////////////////////////
//	������˳�����ʱ��ֹͣ���������Socket���
void CMainDlg::OnBnClickedCancel()
{
	// ֹͣ����
	//m_IOCP.Test();
	SetConfig();
	m_IOCP.Stop();
	CDialog::OnCancel();
}

///////////////////////////////////////////////////////////////////////
//	ϵͳ�˳���ʱ��Ϊȷ����Դ�ͷţ�ֹͣ���������Socket���
void CMainDlg::OnDestroy()
{
	OnBnClickedCancel();
	::CoUninitialize(); 
	CDialog::OnDestroy();
}

void CMainDlg::OnBnClickedButton1()
{
	// TODO: Add your control notification handler code here
	// �����Ի���ʵ��
	//AttState dlg;
	//// �򿪶Ի���
	//dlg.DoModal();
}

void CMainDlg::OnBnClickedRadioProcedure()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	m_WhichClick = CHOOSE_PROCUDURE;
}

void CMainDlg::OnBnClickedRadioPosthttp()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	m_WhichClick = CHOOSE_POSTHTTP;
}

void CMainDlg::OnBnClickedRadioNone()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
	m_WhichClick = CHOOSE_NONE;
}

void CMainDlg::OnBnClickedCheck1()
{
	// TODO: �ڴ���ӿؼ�֪ͨ����������
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