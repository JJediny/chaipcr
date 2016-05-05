#include "networkmanagerhandler.h"
#include "networkinterfaces.h"
#include "wirelessmanager.h"
#include "qpcrapplication.h"
#include "constants.h"

NetworkManagerHandler::NetworkManagerHandler(const std::string &interfaceName, OperationType type)
{
    _interfaceName = interfaceName;
    _type = type;
}

void NetworkManagerHandler::processData(const boost::property_tree::ptree &requestPt, boost::property_tree::ptree &responsePt)
{
    if (_interfaceName == "wlan")
    {
        if (qpcrApp.wirelessManager()->interfaceName().empty())
        {
            setStatus(Poco::Net::HTTPResponse::HTTP_METHOD_NOT_ALLOWED);
            setErrorString("No WIFI interface is present");

            JsonHandler::processData(requestPt, responsePt);

            return;
        }

        _interfaceName = qpcrApp.wirelessManager()->interfaceName();
    }

    switch (_type)
    {
    case GetStat:
        getStat(responsePt);
        break;

    case SetSettings:
        setSettings(requestPt);
        JsonHandler::processData(requestPt, responsePt);
        break;

    case WifiScan:
        wifiScan(responsePt);
        break;

    case WifiConnect:
        wifiConnect();
        JsonHandler::processData(requestPt, responsePt);
        break;

    case WifiDisconnect:
        wifiDisconnect();
        JsonHandler::processData(requestPt, responsePt);
        break;

    default:
        setStatus(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        setErrorString("Unknown opeation type");

        JsonHandler::processData(requestPt, responsePt);

        break;
    }
}

void NetworkManagerHandler::getStat(boost::property_tree::ptree &responsePt)
{
    NetworkInterfaces::InterfaceState state = NetworkInterfaces::getInterfaceState(_interfaceName);
    NetworkInterfaces::InterfaceSettings settings = NetworkInterfaces::readInterfaceSettings(kNetworkInterfacesFile, _interfaceName);

    responsePt.put("interface", _interfaceName);

    if (!state.isEmpty())
    {
        responsePt.put("state.flags", state.flags);
        responsePt.put("state.address", state.address);
        responsePt.put("state.maskAddress", state.maskAddress);
        responsePt.put("state.broadcastAddress", state.broadcastAddress);
    }

    if (!settings.isEmpty())
    {
        responsePt.put("settings.type", settings.type);

        for (std::map<std::string, std::string>::const_iterator it = settings.arguments.begin(); it != settings.arguments.end(); ++it)
            responsePt.put("settings." + it->first, it->second);
    }

    if (_interfaceName == qpcrApp.wirelessManager()->interfaceName())
    {
        WirelessManager::ConnectionStatus status = qpcrApp.wirelessManager()->connectionStatus();

        switch (status)
        {
        case WirelessManager::Connecting:
            responsePt.put("state.status", "connecting");
            break;

        case WirelessManager::ConnectionError:
            responsePt.put("state.status", "connection_error");
            break;

        case WirelessManager::AuthenticationError:
            responsePt.put("state.status", "authentication_error");
            break;

        case WirelessManager::Connected:
            responsePt.put("state.status", "connected");
            break;

        default:
            responsePt.put("state.status", "not_connected");
            break;
        }
    }
}

void NetworkManagerHandler::setSettings(const boost::property_tree::ptree &requestPt)
{
    if (requestPt.find("type") != requestPt.not_found())
    {
        NetworkInterfaces::InterfaceSettings settings;
        settings.interface = _interfaceName;
        settings.type = requestPt.get<std::string>("type");

        for (boost::property_tree::ptree::const_iterator it = requestPt.begin(); it != requestPt.end(); ++it)
        {
            if (it->first != "type")
                settings.arguments[it->first] = it->second.get_value<std::string>();
        }

        NetworkInterfaces::writeInterfaceSettings(kNetworkInterfacesFile, settings);

        if (_interfaceName != qpcrApp.wirelessManager()->interfaceName())
        {
            NetworkInterfaces::ifdown(_interfaceName);
            NetworkInterfaces::ifup(_interfaceName);
        }
        else
            wifiConnect();
    }
    else
    {
        setStatus(Poco::Net::HTTPResponse::HTTP_BAD_REQUEST);
        setErrorString("type must be set");
    }
}

void NetworkManagerHandler::wifiScan(boost::property_tree::ptree &responsePt)
{
    std::vector<WirelessManager::ScanResult> scanResult = qpcrApp.wirelessManager()->scanResult();
    boost::property_tree::ptree array;

    for (const WirelessManager::ScanResult &result: scanResult)
    {
        boost::property_tree::ptree ptree;
        ptree.put("ssid", result.ssid);
        ptree.put("quality", result.quality);
        ptree.put("siganl_level", result.siganlLevel);

        switch (result.encryption)
        {
        case WirelessManager::ScanResult::WepEncryption:
            ptree.put("encryption", "wep");
            break;

        case WirelessManager::ScanResult::Wpa1Ecryption:
            ptree.put("encryption", "wpa1");
            break;

        case WirelessManager::ScanResult::Wpa2Ecryption:
            ptree.put("encryption", "wpa2");
            break;

        default:
            ptree.put("encryption", "none");
            break;
        }

        array.push_back(std::make_pair("", ptree));
    }

    responsePt.put_child("scan_result", array);
}

void NetworkManagerHandler::wifiConnect()
{
    qpcrApp.wirelessManager()->connect();
}

void NetworkManagerHandler::wifiDisconnect()
{
    qpcrApp.wirelessManager()->shutdown();
}
