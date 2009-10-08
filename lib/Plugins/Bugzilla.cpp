
#include <xmlrpc-c/base.hpp>
#include "abrtlib.h"
#include "Bugzilla.h"
#include "CrashTypes.h"
#include "DebugDump.h"
#include "ABRTException.h"
#include "CommLayerInner.h"

#define XML_RPC_SUFFIX "/xmlrpc.cgi"

CReporterBugzilla::CReporterBugzilla() :
    m_pXmlrpcTransport(NULL),
    m_pXmlrpcClient(NULL),
    m_pCarriageParm(NULL),
    m_sBugzillaURL("https://bugzilla.redhat.com"),
    m_sBugzillaXMLRPC("https://bugzilla.redhat.com" + std::string(XML_RPC_SUFFIX)),
    m_bNoSSLVerify(false),
    m_bLoggedIn(false)
{}

CReporterBugzilla::~CReporterBugzilla()
{}

void CReporterBugzilla::NewXMLRPCClient()
{
    m_pXmlrpcTransport = new xmlrpc_c::clientXmlTransport_curl(
                             xmlrpc_c::clientXmlTransport_curl::constrOpt()
                                 .no_ssl_verifyhost(m_bNoSSLVerify)
                                 .no_ssl_verifypeer(m_bNoSSLVerify)
                             );
    m_pXmlrpcClient = new xmlrpc_c::client_xml(m_pXmlrpcTransport);
    m_pCarriageParm = new xmlrpc_c::carriageParm_curl0(m_sBugzillaXMLRPC);
}

void CReporterBugzilla::DeleteXMLRPCClient()
{
    if (m_pCarriageParm != NULL)
    {
        delete m_pCarriageParm;
        m_pCarriageParm = NULL;
    }
    if (m_pXmlrpcClient != NULL)
    {
        delete m_pXmlrpcClient;
        m_pXmlrpcClient = NULL;
    }
    if (m_pXmlrpcTransport != NULL)
    {
        delete m_pXmlrpcTransport;
        m_pXmlrpcTransport = NULL;
    }
}

PRInt32 CReporterBugzilla::Base64Encode_cb(void *arg, const char *obuf, PRInt32 size)
{
    CReporterBugzilla* bz = static_cast<CReporterBugzilla*>(arg);
    int ii;
    for (ii = 0; ii < size; ii++)
    {
        if (isprint(obuf[ii]))
        {
            bz->m_sAttchmentInBase64 += obuf[ii];
        }
    }
    return 1;
}

void CReporterBugzilla::Login()
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t loginParams;
    map_xmlrpc_params_t ret;
    loginParams["login"] = xmlrpc_c::value_string(m_sLogin);
    loginParams["password"] = xmlrpc_c::value_string(m_sPassword);
    paramList.add(xmlrpc_c::value_struct(loginParams));
    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("User.login", paramList));
    try
    {
        if( (m_sLogin == "") && (m_sPassword=="") )
        {
            log("Empty login and password");
            throw std::string(_("Empty login and password. Please check Bugzilla.conf"));
        }
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
        ret =  xmlrpc_c::value_struct(rpc->getResult());
        std::stringstream ss;
        ss << xmlrpc_c::value_int(ret["id"]);
        log("Login id: %s", ss.str().c_str());
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::Login(): ") + e.what());
    }
    catch (std::string& s)
    {
        throw CABRTException(EXCEP_PLUGIN, s);
    }
}

void CReporterBugzilla::Logout()
{
    xmlrpc_c::paramList paramList;
    paramList.add(xmlrpc_c::value_string(""));
    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("User.logout", paramList));
    try
    {
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::Logout(): ") + e.what());
    }
}

bool CReporterBugzilla::CheckCCAndReporter(const std::string& pBugId)
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t ret;

    paramList.add(xmlrpc_c::value_string(pBugId));
    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("bugzilla.getBug", paramList));
    try
    {
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
        ret = xmlrpc_c::value_struct(rpc->getResult());
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::CheckCCAndReporter(): ") + e.what());
    }
    std::string reporter = xmlrpc_c::value_string(ret["reporter"]);
    if (reporter == m_sLogin)
    {
        return true;
    }
    std::vector<xmlrpc_c::value> ccs = xmlrpc_c::value_array(ret["cc"]).vectorValueValue();
    int ii;
    for (ii = 0; ii < ccs.size(); ii++)
    {
        std::string cc =  xmlrpc_c::value_string(ccs[ii]);
        if (cc == m_sLogin)
        {
            return true;
        }
    }
    return false;
}

void CReporterBugzilla::AddPlusOneCC(const std::string& pBugId)
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t addCCParams;
    map_xmlrpc_params_t ret;
    map_xmlrpc_params_t updates;

    std::vector<xmlrpc_c::value> CCList;
    CCList.push_back(xmlrpc_c::value_string(m_sLogin));
    updates["add_cc"] = xmlrpc_c::value_array(CCList);

    addCCParams["ids"] = xmlrpc_c::value_int(atoi(pBugId.c_str()));
    addCCParams["updates"] = xmlrpc_c::value_struct(updates);

    paramList.add(xmlrpc_c::value_struct(addCCParams));
    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("Bug.update", paramList));
    try
    {
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::AddPlusOneComment(): ") + e.what());
    }
    ret = xmlrpc_c::value_struct(rpc->getResult());
}

std::string CReporterBugzilla::CheckUUIDInBugzilla(const std::string& pComponent, const std::string& pUUID)
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t searchParams;
    map_xmlrpc_params_t ret;
    std::string quicksearch = "ALL component:\""+ pComponent +"\" statuswhiteboard:\""+ pUUID + "\"";
    searchParams["quicksearch"] = xmlrpc_c::value_string(quicksearch.c_str());
    paramList.add(xmlrpc_c::value_struct(searchParams));
    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("Bug.search", paramList));
    try
    {
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::CheckUUIDInBugzilla(): ") + e.what());
    }
    ret = xmlrpc_c::value_struct(rpc->getResult());
    std::vector<xmlrpc_c::value> bugs = xmlrpc_c::value_array(ret["bugs"]).vectorValueValue();
    if (bugs.size() > 0)
    {
        map_xmlrpc_params_t bug;
        std::stringstream ss;

        bug = xmlrpc_c::value_struct(bugs[0]);
        ss << xmlrpc_c::value_int(bug["bug_id"]);

        log("Bug is already reported: %s", ss.str().c_str());
        update_client(_("Bug is already reported: ") + ss.str());

        return ss.str();
    }
    return "";
}

void CReporterBugzilla::CreateNewBugDescription(const map_crash_report_t& pCrashReport, std::string& pDescription)
{
    std::string howToReproduce;
    std::string comment;

    if (pCrashReport.find(CD_REPRODUCE) != pCrashReport.end())
    {
        howToReproduce = "\n\nHow to reproduce\n"
                         "-----\n" +
                         pCrashReport.find(CD_REPRODUCE)->second[CD_CONTENT];
    }
    if (pCrashReport.find(CD_COMMENT) != pCrashReport.end())
    {
       comment = "\n\nComment\n"
                 "-----\n" +
                 pCrashReport.find(CD_COMMENT)->second[CD_CONTENT];
    }
    pDescription = "\nabrt detected a crash.\n" +
                   howToReproduce +
                   comment +
                   "\n\nAdditional information\n"
                   "======\n";

    map_crash_report_t::const_iterator it;
    for (it = pCrashReport.begin(); it != pCrashReport.end(); it++)
    {
        if (it->second[CD_TYPE] == CD_TXT)
        {
            if (it->first !=  CD_UUID &&
                it->first !=  FILENAME_ARCHITECTURE &&
                it->first !=  FILENAME_RELEASE &&
                it->first !=  CD_REPRODUCE &&
                it->first !=  CD_COMMENT)
            {
                pDescription += "\n" + it->first + "\n";
                pDescription += "-----\n";
                pDescription += it->second[CD_CONTENT] + "\n\n";
            }
        }
        else if (it->second[CD_TYPE] == CD_ATT)
        {
            pDescription += "\n\nAttached files\n"
                            "----\n";
            pDescription += it->first + "\n";
        }
        else if (it->second[CD_TYPE] == CD_BIN)
        {
            char buffer[1024];
            snprintf(buffer, 1024, _("Binary file %s will not be reported."), it->first.c_str());
            warn_client(std::string(buffer));
            //update_client(_("Binary file ")+it->first+_(" will not be reported."));
        }
    }
}

void CReporterBugzilla::GetProductAndVersion(const std::string& pRelease,
                                             std::string& pProduct,
                                             std::string& pVersion)
{
    if (pRelease.find("Rawhide") != std::string::npos)
    {
        pProduct = "Fedora";
        pVersion = "rawhide";
        return;
    }
    if (pRelease.find("Fedora") != std::string::npos)
    {
        pProduct = "Fedora";
    }
    else if (pRelease.find("Red Hat Enterprise Linux") != std::string::npos)
    {
        pProduct = "Red Hat Enterprise Linux ";
    }
    std::string::size_type pos = pRelease.find("release");
    pos = pRelease.find(" ", pos) + 1;
    while (pRelease[pos] != ' ')
    {
        pVersion += pRelease[pos];
        if (pProduct == "Red Hat Enterprise Linux ")
        {
            pProduct += pRelease[pos];
        }
        pos++;
    }
}

std::string CReporterBugzilla::NewBug(const map_crash_report_t& pCrashReport)
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t bugParams;
    map_xmlrpc_params_t ret;
    std::string package = pCrashReport.find(FILENAME_PACKAGE)->second[CD_CONTENT];
    std::string component = pCrashReport.find(FILENAME_COMPONENT)->second[CD_CONTENT];
    std::string description;
    std::string release = pCrashReport.find(FILENAME_RELEASE)->second[CD_CONTENT];;
    std::string product;
    std::string version;
    std::stringstream bugId;
    CreateNewBugDescription(pCrashReport, description);
    GetProductAndVersion(release, product, version);

    bugParams["product"] = xmlrpc_c::value_string(product);
    bugParams["component"] =  xmlrpc_c::value_string(component);
    bugParams["version"] =  xmlrpc_c::value_string(version);
    //bugParams["op_sys"] =  xmlrpc_c::value_string("Linux");
    bugParams["summary"] = xmlrpc_c::value_string("[abrt] crash detected in " + package);
    bugParams["description"] = xmlrpc_c::value_string(description);
    bugParams["status_whiteboard"] = xmlrpc_c::value_string("abrt_hash:" + pCrashReport.find(CD_UUID)->second[CD_CONTENT]);
    bugParams["platform"] = xmlrpc_c::value_string(pCrashReport.find(FILENAME_ARCHITECTURE)->second[CD_CONTENT]);
    paramList.add(xmlrpc_c::value_struct(bugParams));

    xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("Bug.create", paramList));
    try
    {
        rpc->call(m_pXmlrpcClient, m_pCarriageParm);
        ret =  xmlrpc_c::value_struct(rpc->getResult());
        bugId << xmlrpc_c::value_int(ret["id"]);
        log("New bug id: %s", bugId.str().c_str());
        update_client(_("New bug id: ") + bugId.str());
    }
    catch (std::exception& e)
    {
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::NewBug(): ") + e.what());
    }
    return bugId.str();
}

void CReporterBugzilla::AddAttachments(const std::string& pBugId, const map_crash_report_t& pCrashReport)
{
    xmlrpc_c::paramList paramList;
    map_xmlrpc_params_t attachmentParams;
    std::vector<xmlrpc_c::value> ret;
    NSSBase64Encoder* base64;

    map_crash_report_t::const_iterator it;
    for (it = pCrashReport.begin(); it != pCrashReport.end(); it++)
    {
        if (it->second[CD_TYPE] == CD_ATT)
        {
            m_sAttchmentInBase64 = "";
            base64 = NSSBase64Encoder_Create(Base64Encode_cb, this);
            if (!base64)
            {
                throw CABRTException(EXCEP_PLUGIN, "CReporterBugzilla::AddAttachemnt(): cannot initialize base64.");
            }

            NSSBase64Encoder_Update(base64,
                                    reinterpret_cast<const unsigned char*>(it->second[CD_CONTENT].c_str()),
                                    it->second[CD_CONTENT].length());
            NSSBase64Encoder_Destroy(base64, PR_FALSE);

            paramList.add(xmlrpc_c::value_string(pBugId));
            attachmentParams["description"] = xmlrpc_c::value_string("File: " + it->first);
            attachmentParams["filename"] = xmlrpc_c::value_string(it->first);
            attachmentParams["contenttype"] = xmlrpc_c::value_string("text/plain");
            attachmentParams["data"] = xmlrpc_c::value_string(m_sAttchmentInBase64);
            paramList.add(xmlrpc_c::value_struct(attachmentParams));
            xmlrpc_c::rpcPtr rpc(new  xmlrpc_c::rpc("bugzilla.addAttachment", paramList));
            try
            {
                rpc->call(m_pXmlrpcClient, m_pCarriageParm);
                ret = xmlrpc_c::value_array(rpc->getResult()).vectorValueValue();
                std::stringstream ss;
                ss << xmlrpc_c::value_int(ret[0]);
                log("New attachment id: %s", ss.str().c_str());
            }
            catch (std::exception& e)
            {
                throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::AddAttachemnt(): ") + e.what());
            }
        }
    }
}

std::string CReporterBugzilla::Report(const map_crash_report_t& pCrashReport, const std::string& pArgs)
{
    std::string package = pCrashReport.find(FILENAME_PACKAGE)->second[CD_CONTENT];
    std::string component = pCrashReport.find(FILENAME_COMPONENT)->second[CD_CONTENT];
    std::string uuid = pCrashReport.find(CD_UUID)->second[CD_CONTENT];
    std::string bugId;


    NewXMLRPCClient();

    m_bLoggedIn = false;
    try
    {
        update_client(_("Checking for duplicates..."));
        bugId = CheckUUIDInBugzilla(component, uuid);
        if ( bugId != "" ) {
            update_client(_("Logging into bugzilla..."));
            Login();
            m_bLoggedIn = true;
            update_client(_("Checking CC..."));
            if (!CheckCCAndReporter(bugId) && m_bLoggedIn)
            {
                AddPlusOneCC(bugId);
            }
            DeleteXMLRPCClient();
            return m_sBugzillaURL + "/show_bug.cgi?id=" + bugId;
        }
        update_client(_("Logging into bugzilla..."));
        Login();
        m_bLoggedIn = true;
    }
    catch (CABRTException& e)
    {
        DeleteXMLRPCClient();
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::Report(): ") + e.what());
        return "";
    }


    update_client(_("Creating new bug..."));
    try
    {
        bugId = NewBug(pCrashReport);
        AddAttachments(bugId, pCrashReport);
        update_client(_("Logging out..."));
        Logout();
    }
    catch (CABRTException& e)
    {
        DeleteXMLRPCClient();
        throw CABRTException(EXCEP_PLUGIN, std::string("CReporterBugzilla::Report(): ") + e.what());
    }


    DeleteXMLRPCClient();
    return m_sBugzillaURL + "/show_bug.cgi?id=" + bugId;

}

void CReporterBugzilla::SetSettings(const map_plugin_settings_t& pSettings)
{
    if (pSettings.find("BugzillaURL") != pSettings.end())
    {
        m_sBugzillaURL = pSettings.find("BugzillaURL")->second;
        //remove the /xmlrpc.cgi part from old settings
        //FIXME: can be removed after users are informed about new config format
        std::string::size_type pos = m_sBugzillaURL.find(XML_RPC_SUFFIX);
        if(pos != std::string::npos)
        {
            m_sBugzillaURL.erase(pos);
        }
        //remove the trailing '/'
        while (m_sBugzillaURL[m_sBugzillaURL.length() - 1] == '/')
        {
            m_sBugzillaURL.erase(m_sBugzillaURL.length() - 1);
        }
        /*
        if(*(--m_sBugzillaURL.end()) == '/')
        {
            m_sBugzillaURL.erase(--m_sBugzillaURL.end());
        }
        */
        m_sBugzillaXMLRPC = m_sBugzillaURL + std::string(XML_RPC_SUFFIX);
    }
    if (pSettings.find("Login") != pSettings.end())
    {
        m_sLogin = pSettings.find("Login")->second;
    }
    if (pSettings.find("Password") != pSettings.end())
    {
        m_sPassword = pSettings.find("Password")->second;
    }
    if (pSettings.find("NoSSLVerify") != pSettings.end())
    {
        m_bNoSSLVerify = pSettings.find("NoSSLVerify")->second == "yes";
    }
}

map_plugin_settings_t CReporterBugzilla::GetSettings()
{
    map_plugin_settings_t ret;

    ret["BugzillaURL"] = m_sBugzillaURL;
    ret["Login"] = m_sLogin;
    ret["Password"] = m_sPassword;
    ret["NoSSLVerify"] = m_bNoSSLVerify ? "yes" : "no";

    return ret;
}

PLUGIN_INFO(REPORTER,
            CReporterBugzilla,
            "Bugzilla",
            "0.0.3",
            "Check if a bug isn't already reported in a bugzilla "
            "and if not, report it.",
            "zprikryl@redhat.com",
            "https://fedorahosted.org/abrt/wiki",
            PLUGINS_LIB_DIR"/Bugzilla.GTKBuilder");
