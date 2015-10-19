#include <Poco/Net/HTTPServerRequest.h>
#include <Poco/URI.h>
#include <Poco/SHA1Engine.h>

#include <utility>
#include <boost/tokenizer.hpp>

#include "httpcodehandler.h"
#include "testcontrolhandler.h"
#include "statushandler.h"
#include "controlhandler.h"
#include "settingshandler.h"
#include "logdatahandler.h"
#include "wirelessmanagerhandler.h"

#include "qpcrrequesthandlerfactory.h"
#include "experimentcontroller.h"

using namespace std;
using namespace Poco;
using namespace Poco::Net;

const boost::chrono::hours USER_CACHE_DURATION(1);

////////////////////////////////////////////////////////////////////////////////
// Class QPCRRequestHandlerFactory
HTTPRequestHandler* QPCRRequestHandlerFactory::createRequestHandler(const HTTPServerRequest &request)
{
    try
    {
        vector<string> requestPath;
        URI(request.getURI()).getPathSegments(requestPath);

        if (!requestPath.empty())
        {
            if (checkUserAuthorization(request))
            {
                string method = request.getMethod();

                if (method == "OPTIONS" && request.has("Access-Control-Request-Method"))
                    method = request.get("Access-Control-Request-Method");

                if (method == "GET")
                {
                    if (requestPath.at(0) == "status")
                        return new StatusHandler();
                    else if (requestPath.at(0) == "wifi")
                    {
                        if (requestPath.at(1) == "scan")
                            return new WirelessManagerHandler(WirelessManagerHandler::Scan);
                        else if (requestPath.at(1) == "status")
                            return new WirelessManagerHandler(WirelessManagerHandler::Status);
                    }
                }
                else if (method == "PUT")
                {
                    if (requestPath.at(0) == "testControl")
                        return new TestControlHandler();
                    else if (requestPath.at(0) == "settings")
                        return new SettingsHandler();
                    else if (requestPath.at(0) == "logData")
                        return new LogDataHandler();
                }
                else if (method == "POST")
                {
                    if (requestPath.at(0) == "control")
                    {
                        if (requestPath.at(1) == "start")
                            return new ControlHandler(ControlHandler::StartExperiment);
                        else if (requestPath.at(1) == "resume")
                            return new ControlHandler(ControlHandler::ResumeExperiment);
                        else if (requestPath.at(1) == "stop")
                            return new ControlHandler(ControlHandler::StopExperiment);
                    }
                    else if (requestPath.at(0) == "wifi")
                    {
                        if (requestPath.at(1) == "connect")
                            return new WirelessManagerHandler(WirelessManagerHandler::Connect);
                        else if (requestPath.at(1) == "shutdown")
                            return new WirelessManagerHandler(WirelessManagerHandler::Shutdown);
                    }
                }
            }
            else
                return new JSONHandler(HTTPResponse::HTTP_UNAUTHORIZED, "You must be logged in");
        }

        return new HTTPCodeHandler(HTTPResponse::HTTP_NOT_FOUND);
    }
    catch (const std::exception &ex)
    {
        return new JSONHandler(HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, std::string("Exception occured: ") + ex.what());
    }
    catch (...)
    {
        return new JSONHandler(HTTPResponse::HTTP_INTERNAL_SERVER_ERROR, "Unknown exception occured");
    }
}

bool QPCRRequestHandlerFactory::checkUserAuthorization(const HTTPServerRequest &request)
{
    std::string token;

    if (request.has("Authorization"))
    {
        token = request.get("Authorization");
        token.erase(0, 6); //Remove "Token "
    }
    else
    {
        std::string query = URI(request.getURI()).getQuery();
        boost::tokenizer<boost::char_separator<char>> tokens(query, boost::char_separator<char>("&"));

        for (const std::string &argument: tokens)
        {
            if (argument.find("access_token=") == 0)
            {
                token = argument.substr(std::string("access_token=").size());
                break;
            }
        }
    }

    if (!token.empty())
    {
        Poco::SHA1Engine engine;
        engine.update(token);

        token = Poco::SHA1Engine::digestToHex(engine.digest());

        int id = getCachedUserId(token);

        if (id != -1)
            return true;
        else
        {
            id = ExperimentController::getInstance()->getUserId(Poco::SHA1Engine::digestToHex(engine.digest()));

            if (id != -1)
            {
                addCachedUser(token, id);
                return true;
            }
        }
    }

    return false;
}

int QPCRRequestHandlerFactory::getCachedUserId(const string &token)
{
    std::unordered_map<std::string, CachedUser>::iterator it = _cachedUsers.find(token);

    if (it != _cachedUsers.end())
    {
        if ((boost::chrono::system_clock::now() - it->second.cacheTime) < USER_CACHE_DURATION)
            return it->second.id;
        else
            _cachedUsers.erase(it);
    }

    return -1;
}

void QPCRRequestHandlerFactory::addCachedUser(const string &token, int id)
{
    _cachedUsers.insert(std::make_pair(token, CachedUser(id)));
}
