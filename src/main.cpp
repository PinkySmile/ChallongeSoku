#include <iostream>
#include <SFML/Graphics.hpp>
#include <TGUI/TGUI.hpp>
#include <Socket.hpp>
#include <json.hpp>
#include <thread>
#include <Exceptions.hpp>
#define private public
#include <Participant.hpp>
#include <Tournament.hpp>
#include <Match.hpp>
#undef private
#include <JsonUtils.hpp>
#include <Client.hpp>
#include "SecuredWebSocket.hpp"
#include "Utils.hpp"

#if !defined(USERNAME) || !defined(APIKEY)
#undef USERNAME
#undef APIKEY
#define USERNAME ""
#define APIKEY ""
#endif

using namespace ChallongeSoku;

struct ChallongeWSock {
	SecuredWebSocket socket;
	std::string clientId;
	std::string id;
	std::thread socketThread;
};

struct Settings {
	std::string apikey;
	std::string username;
	std::string sshost;
	unsigned short ssport;
	float refreshRate;
	bool useChallongeUsernames;
	tgui::Color noStartedColor;
	tgui::Color hostingColor;
	tgui::Color playingColor;
	tgui::Color winnerColor;
	tgui::Color loserColor;
	std::map<std::string, std::string> roundNames;
};

typedef std::vector<std::shared_ptr<ChallongeAPI::Match>> Round;
typedef std::vector<Round> RobinBracket;
typedef std::map<int, Round> ElimBracket;

struct Bracket {
	std::string type;
	ElimBracket elim;
	RobinBracket robbin;
	std::pair<int, int> roundBounds;
	std::shared_ptr<ChallongeAPI::Match> last;
};

typedef std::map<size_t, Bracket> Pool;

struct State {
	std::thread updateBracketThread;
	std::vector<std::thread> messages;
	sf::RenderWindow win;
	tgui::Gui gui;
	sf::Clock countdown;
	ChallongeWSock wsock;
	Settings settings;
	std::string currentTournament;
	std::thread stateUpdateThread;
	std::map<size_t, std::string> matchesStates;
	std::shared_ptr<ChallongeAPI::Tournament> tournament;
	std::map<size_t, std::shared_ptr<ChallongeAPI::Match>> matches;
	std::map<size_t, std::shared_ptr<ChallongeAPI::Participant>> participants;
	Pool group;
	Bracket bracket;
	bool displayMutex;

	ChallongeAPI::Client client;
	sf::Texture defaultTexture;
	std::map<std::string, sf::Texture> images;
};

void lockMutex(bool &mutex)
{
	while (mutex) std::this_thread::sleep_for(std::chrono::milliseconds(1));
	mutex = true;
}

void openMsgBox(State &state, const std::string &title, const std::string &desc, int variate)
{
	auto fct = [desc, title, variate] {
#ifdef _WIN32
		MessageBoxA(nullptr, desc.c_str(), title.c_str(), variate);
#else
		Utils::dispMsg(title, desc, variate);
#endif
	};

	state.messages.emplace_back(fct);
}

void handleEvents(State &state)
{
	sf::Event event;

	while (state.win.pollEvent(event)) {
		state.gui.handleEvent(event);
		switch (event.type) {
		case sf::Event::Closed:
			state.win.close();
			break;
		case sf::Event::Resized:
			state.gui.setView(sf::View{sf::FloatRect{
				0,
				0,
				static_cast<float>(state.win.getSize().x),
				static_cast<float>(state.win.getSize().y)
			}});
			break;
		default:
			break;
		}
	}
}

void incrementId(std::string &id, int index = -2)
{
	if (index <= -2)
		index = id.size() - 1;

	if (index == -1) {
		id.reserve(index + 1);
		id = "1" + id;
		for (unsigned i = index + 1; i < id.size(); i++)
			id[i] = '0';
		return;
	}

	char c = id[index];

	c++;
	if (c == ':')
		c = 'a';
	if (c == '{')
		return incrementId(id, index - 1);
	id[index] = c;
	for (unsigned i = index + 1; i < id.size(); i++)
		id[i] = '0';
}

void sendWebSocketMessage(ChallongeWSock &sock, const std::string &channel, nlohmann::json value)
{
	value["channel"] = channel;
	value["clientId"] = sock.clientId;
	value["id"] = sock.id;
	incrementId(sock.id);
	std::cout << "Sending " << value.dump(4) << std::endl;
	sock.socket.send(nlohmann::json::array({
		value
	}).dump());
}

void updateTournamentState(State &state, nlohmann::json wsockPayload)
{
	for (auto &round : wsockPayload["matches_by_round"].items()) {
		for (auto &match : round.value()) {
			auto it = state.matches.find(match["id"]);

			if (it != state.matches.end()) {
				auto &obj = it->second;

				ChallongeAPI::getFromJson(obj->_forfeited, "forfeited", match);
				ChallongeAPI::getFromJson(obj->_loserId,   "loser_id", match);
				ChallongeAPI::getFromJson(obj->_winnerId,  "winner_id", match);
				ChallongeAPI::getFromJson(obj->_player1Id, "id", match["player1"]);
				ChallongeAPI::getFromJson(obj->_player2Id, "id", match["player2"]);
				ChallongeAPI::getFromJson(obj->_state,     "state", match);
				ChallongeAPI::getFromJson(obj->_scores,    "scores", match);
			} else {
				std::cerr << match.dump(4) << " ignored" << std::endl;
			}
		}
	}
}

sf::Texture &getTexture(State &state, const std::string &link)
{
	try {
		if (state.images.find(link) != state.images.end())
			return state.images[link];

		sf::Image image;
		ChallongeAPI::SecuredSocket socket;
		auto tmp = link.substr(link.find("//") + 2);
		ChallongeAPI::Socket::HttpRequest request;

		request.host = tmp.substr(0, tmp.find('/'));
		if (tmp.find('/') == std::string::npos)
			request.path = "/";
		else
			request.path = tmp.substr(tmp.find('/'));
		request.httpVer = "HTTP/1.1";
		request.method = "GET";
		request.portno = 443;

		auto response = socket.makeHttpRequest(request);

		if (response.returnCode / 100 == 3) {
			auto &t = getTexture(state, response.header["Location"]);

			state.images[link] = t;
			return t;
		}
		if (!image.loadFromMemory(response.body.c_str(), response.body.size())) {
			std::cerr << link << ": Parsing failed" << std::endl;
			return state.defaultTexture;
		}
		state.images[link].loadFromImage(image);
		return state.images[link];
	} catch (ChallongeAPI::NetworkException &e) {
		std::cerr << link << ": " << e.what() << std::endl;
		return state.defaultTexture;
	}
}

void updateMatchSidePanel(State &state, tgui::Panel::Ptr pan, const ChallongeAPI::Match &match, bool player1)
{
	auto &scores = match.getScores();
	auto prerequ = player1 ? match.isPlayer1IsPrereqMatchLoser() : match.isPlayer2IsPrereqMatchLoser();
	auto &prerequIds = match.getPrerequisiteMatchIds();
	auto otherId = prerequIds.empty() ? std::optional<size_t>{} : (prerequIds.size() == 1 ? prerequIds[0] : prerequIds[!player1]);
	auto playerId = player1 ? match.getPlayer1Id() : match.getPlayer2Id();
	auto score = scores ? (player1 ? scores->first : scores->second) : std::optional<int>{};

	auto username = pan->get<tgui::Label>("Username");
	auto textbox = pan->get<tgui::TextBox>("TextBox");
	auto picture = pan->get<tgui::Picture>("ProfilePic");
	auto scoreLabel = pan->get<tgui::Label>("Score");

	auto other = otherId ? state.matches[*otherId] : std::optional<std::shared_ptr<ChallongeAPI::Match>>{};
	auto participant = playerId ? state.participants[*playerId] : std::optional<std::shared_ptr<ChallongeAPI::Participant>>{};
	auto isWinner = playerId && match.getWinnerId() && match.getWinnerId() == playerId;
	auto isLoser = playerId && match.getLoserId() && match.getLoserId() == playerId;

	auto replacementStr = (prerequ ? "Loser of " : "Winner of ") + (other ? std::to_string((*other)->getSuggestedPlayOrder()) : "");

	if (participant) {
		if (*participant) {
			if ((*participant)->getAttachedParticipatablePortraitUrl())
				picture->getRenderer()->setTexture(getTexture(state, *(*participant)->getAttachedParticipatablePortraitUrl()));
			username->setText((*participant)->getName());
			username->getRenderer()->setTextColor("black");
		} else {
			username->setText("Invalid participant " + std::to_string(*playerId));
			username->getRenderer()->setTextColor("red");
		}
	} else {
		username->setText(replacementStr);
		username->getRenderer()->setTextColor("#AAAAAA");
	}
	textbox->getRenderer()->setBackgroundColor(
		isLoser ? state.settings.loserColor :
		(isWinner ? state.settings.winnerColor : state.settings.noStartedColor)
	);
	scoreLabel->setText(score ? std::to_string(*score) : "-");
}

void updateBracketState(State &state, bool hasThread = true)
{
	if (state.updateBracketThread.joinable() && hasThread)
		state.updateBracketThread.join();

	auto panel = state.gui.get<tgui::Panel>("Bracket");
	auto fct = [&state, panel]{
		for (auto &match : state.matches) {
			auto pan = state.gui.get<tgui::Panel>("Match" + std::to_string(match.first));
			auto top = pan->get<tgui::Panel>("TopPanel");
			auto bot = pan->get<tgui::Panel>("BottomPanel");

			updateMatchSidePanel(state, top, *match.second, true);
			updateMatchSidePanel(state, bot, *match.second, false);
		}
	};

	std::cout << "Updating bracket state" << std::endl;
	if (hasThread)
		state.updateBracketThread = std::thread(fct);
	else
		fct();
}
//TODO: https://hisouten.challonge.com/fr/soku2020
tgui::Panel::Ptr addMatch(const ChallongeAPI::Match &match, tgui::Layout2d pos, tgui::Panel::Ptr panel)
{
	static std::shared_ptr<tgui::Panel> panBase = nullptr;

	if (!panBase) {
		panBase = tgui::Panel::create({200, 41});
		panBase->loadWidgetsFromFile("gui/match.gui");
	}

	auto pan = tgui::Panel::copy(panBase);
	auto id = pan->get<tgui::Label>("ID");

	pan->setPosition(pos);
	id->setText(std::to_string(match.getSuggestedPlayOrder()));
	panel->add(pan, "Match" + std::to_string(match.getId()));
	return pan;
}

void buildRoundRobbinBracket(const RobinBracket &bracket, const tgui::Panel::Ptr &panel)
{
	sf::Vector2u size = {0, 0};
	size_t lastSize = 0;
	tgui::Vector2f pos {10, 10};
	std::string id = "A";

	std::cout << "Building round robbin bracket" << std::endl;
	for (size_t round = 0; round < bracket.size(); round++) {
		auto lab = tgui::Label::create("Round " + std::to_string(round + 1));

		pos.y = 10;
		lab->setSize(200, 20);
		lab->setPosition(pos);
		lab->setVerticalAlignment(tgui::Label::VerticalAlignment::Center);
		lab->setHorizontalAlignment(tgui::Label::HorizontalAlignment::Center);
		lab->getRenderer()->setTextColor("white");
		lab->getRenderer()->setBackgroundColor("black");
		panel->add(lab);
		pos.y += 20;
		for (auto &match : bracket[round]) {
			addMatch(*match, pos, panel);
			pos.y += 60;
		}
		pos.x += 210;
		size.x = std::max<unsigned>(size.x, pos.x);
		size.y = std::max<unsigned>(size.y, pos.y - 10);
	}
	panel->setSize(size.x, size.y);
	panel->setPosition(0, 0);
	panel->getRenderer()->setBackgroundColor("#999999");
}

void buildSimpleElimBracket(const ElimBracket &bracket, const tgui::Panel::Ptr &panel)
{
	sf::Vector2u size = {0, 0};
	size_t biggest = 0;

	for (auto &matches : bracket)
		biggest = std::max(biggest, matches.second.size());
	size.y = biggest * 60 + 20;
	for (auto &matches : bracket) {
		auto lab = tgui::Label::create("Round " + std::to_string(matches.first));
		tgui::Vector2f pos;
		size_t step = 60;

		pos.x = (std::abs(matches.first) - 1) * 240.f + 10;
		pos.y = 10;
		lab->setSize(200, 20);
		lab->setPosition(pos);
		lab->setVerticalAlignment(tgui::Label::VerticalAlignment::Center);
		lab->setHorizontalAlignment(tgui::Label::HorizontalAlignment::Center);
		lab->getRenderer()->setTextColor("white");
		lab->getRenderer()->setBackgroundColor("black");
		panel->add(lab);
		pos.y += 20;
		if (std::abs(matches.first) != 1) {
			auto inc = 30;
			auto i = biggest, j = matches.second.size();

			while (i && j) {
				i >>= 1U;
				j >>= 1U;
			}
			while (i || j) {
				i >>= 1U;
				j >>= 1U;
				pos.y += inc;
				step += inc * 2;
				inc *= 2;
			}
		}

		size.x = std::max<unsigned>(size.x, pos.x + 210);
		panel->setSize(size.x, size.y);
		for (auto &match : matches.second) {
			addMatch(*match, pos, panel);
			pos.y += step;
		}
	}
	panel->setSize(size.x, size.y);
}

void buildDoubleElimBracket(const ElimBracket &bracket, const tgui::Panel::Ptr &pan)
{
	ElimBracket loser;
	ElimBracket winner;
	auto loserPanel = tgui::Panel::create();
	auto winnerPanel = tgui::Panel::create();

	for (auto &matches : bracket)
		(matches.first < 0 ? loser : winner)[matches.first] = matches.second;

	buildSimpleElimBracket(winner, winnerPanel);
	buildSimpleElimBracket(loser, loserPanel);
	loserPanel->setPosition(0, "winnerBracket.h");
	loserPanel->getRenderer()->setBackgroundColor("#888888");
	winnerPanel->setPosition(0, 0);
	winnerPanel->getRenderer()->setBackgroundColor("#AAAAAA");
	pan->add(winnerPanel, "winnerBracket");
	pan->add(loserPanel, "loserBracket");
	pan->setSize(
		std::max(winnerPanel->getSize().x, loserPanel->getSize().x),
		loserPanel->getPosition().y + loserPanel->getSize().y
	);
}

inline void buildBracket(const Bracket &bracket, const tgui::Panel::Ptr &pan)
{
	std::cout << "Building bracket of type " << bracket.type << std::endl;
	if (bracket.type == "double elimination")
		buildDoubleElimBracket(bracket.elim, pan);
	else if (bracket.type == "single elimination")
		buildSimpleElimBracket(bracket.elim, pan);
	else
		buildRoundRobbinBracket(bracket.robbin, pan);
}

void buildGroupBrackets(const Pool &pools, const tgui::Panel::Ptr &pan)
{
	char labText[] = "Pool A";
	char labName[] = "LabelPool@";
	char finalX[] = "10 + Pool@.x + Pool@.w";
	char finalY[] = "10 + Pool@.y + Pool@.h";
	char pos[] = "LabelPool@.y + LabelPool@.h";
	tgui::Vector2f size = {0, 0};

	for (auto &pool : pools) {
		auto panel = tgui::Panel::create();
		auto label = tgui::Label::create(labText);

		label->setPosition(0, finalY);
		label->setTextSize(20);
		label->setVerticalAlignment(tgui::Label::VerticalAlignment::Center);
		label->setHorizontalAlignment(tgui::Label::HorizontalAlignment::Center);
		label->getRenderer()->setTextColor("white");

		labName[sizeof(labName) - 2]++;
		labText[sizeof(labText) - 2]++;
		pos[strlen("LabelPool")]++;
		pos[strlen("LabelPool@.y + LabelPool")]++;
		finalX[strlen("10 + Pool")]++;
		finalY[strlen("10 + Pool")]++;
		finalX[strlen("10 + Pool@.x + Pool")]++;
		finalY[strlen("10 + Pool@.x + Pool")]++;

		pan->add(label, labName);
		pan->add(panel, labName + strlen("Label"));

		buildBracket(pool.second, panel);
		panel->setPosition(0, pos);
		label->setSize(finalX + strlen("10 + Pool@.x + "), 20);

		size.x = std::max(size.x, panel->getSize().x);
		size.y = panel->getPosition().y + panel->getSize().y;
		std::cout << pos << ":" << size.x << ":" << size.y << std::endl;
	}
	pan->setSize(size);
	pan->getRenderer()->setBackgroundColor("#444444");
}

void buildBracketTree(State &state)
{
	auto panel = state.gui.get<tgui::Panel>("Bracket");
	auto groupPanel = tgui::Panel::create();
	auto bracketPanel = tgui::Panel::create();

	lockMutex(state.displayMutex);
	panel->removeAllWidgets();
	state.displayMutex = false;
	buildGroupBrackets(state.group, groupPanel);
	buildBracket(state.bracket, bracketPanel);
	groupPanel->setPosition(0, 0);
	bracketPanel->setPosition("groupPanel.w", 0);
	lockMutex(state.displayMutex);
	panel->add(groupPanel, "groupPanel");
	panel->add(bracketPanel, "bracketPanel");
	state.displayMutex = false;
	updateBracketState(state, false);
}

void connectToWebsocket(ChallongeWSock &wsock)
{
	ChallongeAPI::SecuredSocket sock;
	ChallongeAPI::Socket::HttpRequest requ;

	requ.host = "stream.challonge.com";
	requ.portno = 8000;
	requ.method = "GET";
	requ.httpVer = "HTTP/1.1";
	requ.path = "/faye?message=%5B%7B%22channel%22%3A%22%2Fmeta%2Fhandshake%22%2C%22version%22%3A%221.0%22%2C%22supportedConnectionTypes%22%3A%5B%22websocket%22%2C%22eventsource%22%2C%22long-polling%22%2C%22cross-origin-long-polling%22%2C%22callback-polling%22%5D%2C%22id%22%3A%221%22%7D%5D&jsonp=__jsonp1__";

	auto result = sock.makeHttpRequest(requ);
	auto pos = result.body.find("__jsonp1__");
	auto data = nlohmann::json::parse(result.body.substr(pos + 11, result.body.size() - pos - 13))[0];

	wsock.clientId = data["clientId"];
	wsock.socket.setPath("/faye");
	wsock.socket.connect("stream.challonge.com", 8000);
	sendWebSocketMessage(wsock, "/meta/connect", {{"connectionType", "websocket"}});
}

inline void addMatchToBracket(const std::shared_ptr<ChallongeAPI::Match> &match, Bracket &bracket)
{
	bracket.elim[match->getRound()].push_back(match);
	if (match->getRound() < 0)
		return;
	while (bracket.robbin.size() < match->getRound())
		bracket.robbin.emplace_back();
	bracket.robbin[match->getRound() - 1].push_back(match);
}

inline void addMatchToPool(const std::shared_ptr<ChallongeAPI::Match> &match, Pool &pool)
{
	addMatchToBracket(match, pool[*match->getGroupId()]);
}

std::string getGroupStageType(State &state, const Pool &pools)
{
	if (pools.empty())
		return "who cares";

	bool hadZero = false;
	bool diffSize = false;
	size_t expected = 0;
	size_t nbMatchs = 0;
	size_t nbRounds = 0;
	size_t participantsPerPool = state.tournament->getParticipantsCount() / pools.size();

	for (auto &bracket : pools) {
		diffSize |= nbRounds && nbRounds != bracket.second.robbin.size();
		nbRounds = bracket.second.robbin.size();
		for (auto &round : bracket.second.elim) {
			if (round.first < 0)
				return "double elimination";
			hadZero |= round.second.empty();
			diffSize |= nbMatchs && nbMatchs != round.second.size();
			nbMatchs = round.second.size();
		}
	}
	if (state.tournament->getParticipantsCount() % pools.size() || hadZero || diffSize || nbMatchs * nbRounds != participantsPerPool * (participantsPerPool - 1) / 2)
		return "single elimination";
	return "round robbin";
}

void loadChallongeTournament(State &state, const std::string &url, bool noObjectRefresh = false)
{
	state.currentTournament.clear();
	if (state.wsock.socket.isOpen())
		state.wsock.socket.disconnect();
	if (state.wsock.socketThread.joinable())
		state.wsock.socketThread.join();
	state.wsock.id = "2";
	auto fct = [&state, url, noObjectRefresh] {
		auto score = state.gui.get<tgui::Label>("Score");

		if (!noObjectRefresh) {
			state.currentTournament.clear();
			state.gui.get<tgui::Label>("Score")->setText("Loading tournament " + url + "...");
			state.tournament = state.client.getTournamentByName(url);
		}
		std::cout << "Tournament type is " << state.tournament->getTournamentType() << std::endl;
		if (state.tournament->getTournamentType() == "swiss")
			throw ChallongeAPI::NotImplementedException("Swiss tournaments are not yet implemented. Sorry....");
		if (state.tournament->getGameName() != "Touhou Hisoutensoku")
			openMsgBox(
				state,
				"Game not supported",
				"Warning: This tournament's game is " + state.tournament->getGameName() + " but it is not supported.\nYou won't be able to use this program to connect to games.",
				MB_ICONWARNING
			);
		connectToWebsocket(state.wsock);
		sendWebSocketMessage(
			state.wsock,
			"/meta/subscribe",
			{
				{"subscription", "/tournaments/" + std::to_string(state.tournament->getId())}
			}
		);
		state.wsock.socketThread = std::thread([&state]{
			auto score = state.gui.get<tgui::Label>("Score");
			std::string tournamentChan = "/tournaments/" + std::to_string(state.tournament->getId());

			while (true)
				try {
					std::string data = state.wsock.socket.getAnswer();
					auto parsed = nlohmann::json::parse(data);

					for (auto &elem : parsed) {
						std::cout << "Received " << elem.dump(4) << std::endl;
						std::string channel = elem["channel"];

						if (channel == "/meta/subscribe" && !elem["successful"])
							return openMsgBox(state, "Websocket error", "Cannot subscribe to tournament events:\n\n" + elem["error"].get<std::string>(), MB_ICONERROR);
						else if (channel == "/meta/connect")
							sendWebSocketMessage(state.wsock, "/meta/connect",{{"connectionType", "websocket"}});
						else if (channel == tournamentChan) {
							updateTournamentState(state, elem["data"]["TournamentStore"]);
							updateBracketState(state);
						}
					}
				} catch (ChallongeAPI::ConnectionTerminatedException &e) {
					return;
				} catch (ChallongeAPI::EOFException &e) {
					if (state.wsock.socket.isOpen())
						openMsgBox(state, "Websocket error: ChallongeAPI::EOFException", e.what(), MB_ICONERROR);
					return;
				} catch (std::exception &e) {
					openMsgBox(state, "Websocket error: " + Utils::getLastExceptionName(), e.what(), MB_ICONERROR);
					return;
				}
		});
		state.matchesStates.clear();
		state.participants.clear();
		state.matches.clear();
		state.group.clear();
		state.bracket.type = state.tournament->getTournamentType();
		state.bracket.elim.clear();
		state.bracket.robbin.clear();
		state.bracket.roundBounds.first = INT32_MAX;
		state.bracket.roundBounds.second = INT32_MIN;

		for (auto &participant : state.tournament->getParticipants()) {
			state.participants[participant->getId()] = participant;
			for (auto &alt : participant->getGroupPlayerIds())
				state.participants[alt] = participant;
		}

		std::cout << "Building round tree" << std::endl;
		for (auto &match : state.tournament->getMatches()) {
			state.matches[match->getId()] = match;
			std::cout << match->getId() << std::endl;
			std::cout << (match->getGroupId() ? std::to_string(*match->getGroupId()) : "None") << std::endl;
			std::cout << match->getState() << std::endl;
			std::cout << match->getRound() << ":" << match->getSuggestedPlayOrder() << std::endl
				  << std::endl;

			if (match->getGroupId())
				addMatchToPool(match, state.group);
			else
				addMatchToBracket(match, state.bracket);
		}

		auto type = getGroupStageType(state, state.group);

		for (auto &elem : state.group)
			elem.second.type = type;
		std::cout << "Building bracket tree GUI" << std::endl;
		buildBracketTree(state);
		std::cout << "Done" << std::endl;
		state.currentTournament = url;
		score->setText(state.tournament->getName());
		score->getRenderer()->setTextColor("black");
	};

	if (state.updateBracketThread.joinable())
		state.updateBracketThread.join();
	state.updateBracketThread = std::thread(fct);
}

void refreshView(State &state)
{
	if (state.stateUpdateThread.joinable())
		state.stateUpdateThread.join();
	state.countdown.restart();
	state.stateUpdateThread = std::thread([&state]{
		ChallongeAPI::Socket sock;
		auto score = state.gui.get<tgui::Label>("Warning");
		auto chw = state.gui.get<tgui::Label>("ChallongeWarning");

		try {
			ChallongeAPI::Socket::HttpRequest requ;

			requ.portno = state.settings.ssport;
			requ.host = state.settings.sshost;
			requ.httpVer = "HTTP/1.1";
			requ.method = "GET";
			requ.path = "/connect";

			auto res = sock.makeHttpRequest(requ);

			score->getRenderer()->setTextColor("#FF8800");
			score->setText("Warning: Invalid SokuStreaming version: GET to /connect returned " + std::to_string(res.returnCode) + " " + res.codeName);
		} catch (ChallongeAPI::HTTPErrorException &e) {
			switch (e.getResponse().returnCode) {
			case 404:
				score->getRenderer()->setTextColor("#FF8800");
				return score->setText("Warning: Invalid SokuStreaming version: GET to /connect returned 404 " + e.getResponse().codeName);
			case 403:
				score->getRenderer()->setTextColor("#FF8800");
				return score->setText("Warning: SokuStreaming refused access to /connect: " + e.getResponse().codeName);
			case 405:
				score->getRenderer()->setTextColor("green");
				return score->setText("SokuStreaming works");
			default:
				score->getRenderer()->setTextColor("#FF8800");
				score->setText("Warning: Invalid SokuStreaming version: GET to /connect returned " + std::to_string(e.getResponse().returnCode) + " " + e.getResponse().codeName);
			}
		} catch (std::exception &e) {
			std::cerr << e.what() << std::endl;
			score->getRenderer()->setTextColor("red");
			score->setText("Warning: Cannot connect to SokuStreaming: " + std::string(e.what()));
		}

		if (state.currentTournament.empty())
			return;
		try {
			auto oldState = state.tournament;

			state.tournament = state.client.getTournamentByName(state.currentTournament);
			if (oldState->getMatches().size() != state.tournament->getMatches().size())
				loadChallongeTournament(state, state.currentTournament, false);
		} catch (std::exception &e) {
			chw->setText("Cannot refresh tournament: " + std::string(e.what()));
			return;
		}
	});
}

void hookGuiHandlers(State &state)
{
	auto menu = state.gui.get<tgui::MenuBar>("MenuBar");

	menu->connect("Focused", [](std::weak_ptr<tgui::MenuBar> menu){
		menu.lock()->moveToFront();
	}, std::weak_ptr<tgui::MenuBar>(menu));
	menu->connectMenuItem({"Tournament", "Open Challonge tournament"}, [&state]{
		auto win = Utils::openWindowWithFocus(state.gui, 300, 40);
		auto open = tgui::Button::create("Open");
		auto edit = tgui::EditBox::create();

		edit->setDefaultText("Tournament URL");
		edit->setPosition(10, 10);
		edit->setSize("&.w - 30 - open.w", "&.h - 20");
		open->setPosition("&.w - 10 - w", 10);
		open->setSize(open->getSize().x, "&.h - 20");
		open->connect("Clicked", [edit, &state](std::weak_ptr<tgui::ChildWindow> w){
			if (edit->getText().isEmpty())
				return;
			try {
				loadChallongeTournament(state, edit->getText());
				w.lock()->close();
			} catch (ChallongeAPI::HTTPErrorException &e) {
				auto res = e.getResponse();
				std::string msg = e.what();

				if (res.returnCode == 401)
					msg += "\n\nPlease check your credentials.";
				else if (res.returnCode == 404)
					msg += "\n\nPlease check the URL.";
				else if (res.returnCode / 100 == 5)
					msg += "\n\nPlease try again later.";
				else {
					std::cerr << ChallongeAPI::Socket::generateHttpRequest(res.request) << std::endl;
					msg += "\n\nPlease report this to the developer.";
				}
				std::cerr << ChallongeAPI::Socket::generateHttpResponse(res) << std::endl;
				openMsgBox(state, Utils::getLastExceptionName(), msg, MB_ICONERROR);
			} catch (std::exception &e) {
				openMsgBox(state, Utils::getLastExceptionName(), e.what(), MB_ICONERROR);
			}
		}, std::weak_ptr(win));
		win->add(open, "open");
		win->add(edit);
	});
}

int main()
{
	State state{
		.win                   = {
			{640, 480},
			"Challonge Soku"
		},
		.gui                           = {state.win},
		.countdown                     = {},
		.wsock                         = {
			.socket                = {},
			.clientId              = {},
			.id                    = "2",
			.socketThread          = {},
		},
		.settings                      = {
			.apikey                = APIKEY,
			.username              = USERNAME,
			.sshost                = "localhost",
			.ssport                = 80,
			.refreshRate           = 10,
			.useChallongeUsernames = true,
			.noStartedColor        = "white",
			.hostingColor          = "blue",
			.playingColor          = "#FF8800",
			.winnerColor           = "green",
			.loserColor            = "red",
			.roundNames            = {
				{"round", "Round {}"},
				{"-1", "Grand final"},
				{"-2", "Final"},
				{"-3", "Demi-final"},
				{"lround", "Looser round {}"},
				{"l-2", "Looser demi-final"},
				{"l-3", "Looser final"},
			}
		},
		.currentTournament             = {},
		.stateUpdateThread             = {},
		.tournament                    = {},
		.matches                       = {},
		.participants                  = {},
		.group                         = {},
		.bracket                       = {
			.type                  = "",
			.elim                  = {},
			.robbin                = {},
			.roundBounds           = {INT32_MAX, INT32_MIN}
		},
		.client                        = {USERNAME, APIKEY},
		.defaultTexture                = {},
		.images                        = {}
	};

	state.gui.loadWidgetsFromFile("gui/main_screen.gui");

	auto refresh = state.gui.get<tgui::Label>("Refresh");

	hookGuiHandlers(state);
	refreshView(state);
	while (state.win.isOpen()) {
		auto t = state.countdown.getElapsedTime().asSeconds();
		int remain = state.settings.refreshRate - t;

		handleEvents(state);
		if (remain <= 0)
			refresh->setText("Refreshing...");
		else
			refresh->setText("Refreshing in " + std::to_string(remain) + " second" + (remain >= 2 ? "s" : ""));

		if (t >= state.settings.refreshRate)
			refreshView(state);
		if (!state.displayMutex) {
			state.win.clear(sf::Color::White);
			lockMutex(state.displayMutex);
			state.gui.draw();
			state.displayMutex = false;
		}
		state.win.display();
	}
	if (state.stateUpdateThread.joinable())
		state.stateUpdateThread.join();
	if (state.updateBracketThread.joinable())
		state.updateBracketThread.join();
	if (state.wsock.socket.isOpen())
		state.wsock.socket.disconnect();
	if (state.wsock.socketThread.joinable())
		state.wsock.socketThread.join();
	for (auto &thread : state.messages)
		if (thread.joinable())
			thread.join();
	return EXIT_SUCCESS;
}
