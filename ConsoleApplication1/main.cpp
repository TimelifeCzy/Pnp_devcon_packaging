#include <iostream>
#include <Windows.h>

using namespace std;

/*
	@ 导出函数：
		- 参数1：参数数量校验
		- 参数2: 字符串
*/
typedef int (*PnSendBoomNICControl)(_In_ int argc, _In_reads_(argc) PWSTR* argv);
PnSendBoomNICControl nf_SendControl;
HMODULE g_nBoomModuleHandler = NULL;
HANDLE  g_removeEventHand = NULL;

/*
    @ 获取服务状态
        - szSvcName: 服务名
*/
DWORD GetServicesStatus(void)
{
    TCHAR szSvcName[] = L"BoomNIC";
    SC_HANDLE schSCManager = NULL;
    SC_HANDLE schService = NULL;

    SERVICE_STATUS_PROCESS ssStatus;
    DWORD dwOldCheckPoint = 0;
    DWORD dwStartTickCount = 0;
    DWORD dwWaitTime = 0;
    DWORD dwBytesNeeded = 0;

    schSCManager = OpenSCManager(
        NULL,                                // local computer
        NULL,                                // ServicesActive database
        SC_MANAGER_ALL_ACCESS);              // full access rights

    if (NULL == schSCManager)
    {
        printf("OpenSCManager failed (%d)\n", GetLastError());
        return -1;

    }

    schService = OpenService(
        schSCManager,                      // SCM database
        szSvcName,                         // name of service
        SERVICE_QUERY_STATUS |
        SERVICE_ENUMERATE_DEPENDENTS);     // full access

    if (schService == NULL)
    {
        printf("OpenService failed (%d)\n", GetLastError());
        CloseServiceHandle(schSCManager);
        return -1;
    }

    if (!QueryServiceStatusEx(
        schService,                         // handle to service
        SC_STATUS_PROCESS_INFO,             // information level
        (LPBYTE)&ssStatus,                 // address of structure
        sizeof(SERVICE_STATUS_PROCESS),     // size of structure
        &dwBytesNeeded))                  // size needed if buffer is too small
    {
        printf("QueryServiceStatusEx failed (%d)\n", GetLastError());
        CloseServiceHandle(schService);
        CloseServiceHandle(schSCManager);
        return -1;
    }
    return ssStatus.dwCurrentState;
}

DWORD ReStartPnpBoomDriver(void) 
/*
    5. 重启驱动
*/
{
    DWORD nSeriverstatus = -1;
    const WCHAR* nf_sendRestartCon[] = { L"devcondll.exe", L"restart", L"BoomNIC.inf", L"BoomNIC" };
    nf_SendControl(4, (PWSTR*)nf_sendRestartCon);
    nSeriverstatus = GetServicesStatus();
    return nSeriverstatus;
}


DWORD DeletePnpBoomDriver(void)
/*
    9. 卸载/删除驱动
        Windows Pnp设备栈中删除注册和设备，没有删除相关文件和注册服务
        附加操作: 1. sc delete BoomNIC & 2. DeleteFile
*/
{
    if (!g_nBoomModuleHandler) {
        g_nBoomModuleHandler = LoadLibrary(L"boomcon.dll");
        if (!g_nBoomModuleHandler) {
            cout << "Load boomcon faliuer" << endl;
            return false;
        }
    }

    if (!nf_SendControl) {
        nf_SendControl = (PnSendBoomNICControl)GetProcAddress(g_nBoomModuleHandler, "nf_SendControl");
        if (!nf_SendControl)
            return false;
    }

    DWORD nSeriverstatus = -1;
    const WCHAR* nf_sendRemovCon[] = { L"boomcon.exe", L"remove", L"BoomNIC" };
    const WCHAR* nf_sendRemovCon_reboot[] = { L"boomcon.exe", L"/r", L"remove", L"BoomNIC" };
    nf_SendControl(3, (PWSTR*)nf_sendRemovCon);
    if (!g_removeEventHand)
        g_removeEventHand = CreateEvent(NULL, FALSE, FALSE, TEXT("Global\\RemoveEvent"));
    WaitForSingleObject(g_removeEventHand, INFINITE);
    if (g_removeEventHand) {
        CloseHandle(g_removeEventHand);
        g_removeEventHand = NULL;
    }

    nSeriverstatus = GetServicesStatus();
    switch (nSeriverstatus)
    {
        // 移除后应该是STOP或者其他状态
    case SERVICE_CONTINUE_PENDING:
    case SERVICE_RUNNING:
    case SERVICE_START_PENDING:
        cout << "Remove Driver Failuer" << endl;
        // 如果删除失败: 可能设备正在被占用或其他情况。
        //  - 加入参数 /r 重启时候删除设备网卡Pnp驱动，不需要等待。
        nf_SendControl(4, (PWSTR*)nf_sendRemovCon_reboot);
        break;
    }

    // sc删除服务(驱动注册表)
    wstring pszCmd = L"sc delete ServicesName";
    STARTUPINFO si = { sizeof(STARTUPINFO) };
    GetStartupInfo(&si);
    si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
    si.wShowWindow = SW_HIDE;
    // 启动命令行
    PROCESS_INFORMATION pi;
    CreateProcess(NULL, (LPWSTR)pszCmd.c_str(), NULL, NULL, TRUE, NULL, NULL, NULL, &si, &pi);
    Sleep(500);
    // 删除BoomNIC网卡驱动文件 - 理论上sc delete也会删除.
    DeleteFile(L"C:\\Windows\\System32\\drivers\\Driver.sys");
    return 0;
}

int main()
/*
    测试用例：
	@ 2021/8/11
	@ author: zy.chen
		- 针对.inf类型的驱动文件进行Control
		- 删除/更新/禁用/启用/卸载等控制
*/
{
    DWORD nSeriverstatus = -1;

	// 1. 加载devcondll DLL
	auto nBoomModuleHandler = LoadLibrary(L"devcondll.dll");
	if (!nBoomModuleHandler) {
		cout << "Load devcondll faliuer" << endl;
		return false;
	}

	// 2. 获取控制API
	nf_SendControl = (PnSendBoomNICControl)GetProcAddress(nBoomModuleHandler, "nf_SendControl");
	if (!nf_SendControl) {
		cout << "Load nf_SendControl function address failuer" << endl;
		return false;
	}

    // 3. 判断是否存在该驱动服务(虚拟网卡)
    nSeriverstatus = GetServicesStatus();
    switch (nSeriverstatus)
    {
        // 正在运行
    case SERVICE_CONTINUE_PENDING:
    case SERVICE_RUNNING:
    case SERVICE_START_PENDING:
        cout << "Running" << endl;
        return nSeriverstatus;
        // 已停止(已安装)
    case SERVICE_STOPPED:
        // 如果驱动已存，需要重新启动驱动.重启驱动失败，删除驱动
        // 应用场景，电脑重启？或者其他非预期操作。
        cout << "Install Success. But Start Driver failuer - STOP. Checkout Driver Sign or xxx" << endl;
        { 
            // 重启驱动
            nSeriverstatus = ReStartPnpBoomDriver();
            switch (nSeriverstatus)
            {
            case SERVICE_CONTINUE_PENDING:
            case SERVICE_RUNNING:
            case SERVICE_START_PENDING:
                cout << "Restart Driver Running Success." << endl;
                break;
            default:
                cout << "Restart Driver Failuer." << endl;
                // 删除驱动 - 继续执行 - 尝试重新安装启动
                DeletePnpBoomDriver();
                break;
            }
        }          
        break;
    }

	// 4. 安装驱动(提示安装网络适配器)
	const WCHAR *nf_sendInstallCon[] = {L"devcondll.exe", L"install", L"Driver.inf", L"ServicesName"};
    nf_SendControl(4, (PWSTR*)nf_sendInstallCon);
    nSeriverstatus = GetServicesStatus();
    switch (nSeriverstatus)
    {
    case SERVICE_CONTINUE_PENDING:
    case SERVICE_RUNNING:
    case SERVICE_START_PENDING:
        cout << "Install Driver（BoomNIC） Success" << endl;
        break;
    default:
        cout << "Install failuer" << endl;
        return nSeriverstatus;
    }
    
	//// 5. 更新驱动网卡(热补丁 - 驱动替换)
	//const WCHAR *nf_sendUpdateCon[] = { L"devcondll.exe", L"update", L"BoomNIC.inf", L"BoomNIC" };
	//nf_SendControl(4, (PWSTR*)nf_sendUpdateCon);
    //nSeriverstatus = GetServicesStatus(); -- 更新后驱动状态：运行

    // 6. 禁用设备网卡
    //const WCHAR* nf_sendDisableCon[] = { L"devcondll.exe", L"disable", L"BoomNIC" };
    //nf_SendControl(3, (PWSTR*)nf_sendDisableCon);
    //system("pause");


    // 7. 启动设备网卡
    //const WCHAR* nf_sendEnableCon[] = { L"devcondll.exe", L"enable", L"BoomNIC" };
    //nf_SendControl(3, (PWSTR*)nf_sendEnableCon);    
	//system("pause");
	return 0;
}
