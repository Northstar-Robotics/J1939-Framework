#include <SPN/SPNHistory.h>
#include <Utils.h>

#include <unordered_map>
#include <json/json.h>

#include "graph.h"
#include "graph.pb.h"

Graph getGraphFromSPN(u32 id, u32 spn, u32 samples, u32 period);

using namespace J1939;
using namespace Utils;

std::unordered_map<u64/*canid | spnNumber >> 32*/,SPNHistory> historyMap;

typedef struct {
	Graph graph;
	bool toSend;
} UserData;


void saveToHistory(u32 id, const SPN& spn, const TimeStamp& timestamp) {

	//Compose the key for the map. Key = canid + spnNumber
	u64 key = (u64)(id) | ((u64)(spn.getSpnNumber()) << 32);

	SPNHistory& history = historyMap[key];

	history.addSample(TimeStamp::now() ,spn);

}


int callback_graph(struct lws *wsi, enum lws_callback_reasons reason,
		void *user, void *in, size_t len) {

	switch (reason) {

	case LWS_CALLBACK_ESTABLISHED: {

		GOOGLE_PROTOBUF_VERIFY_VERSION;

		UserData *userData = new UserData();

		*(static_cast<UserData**>(user)) = userData;

	}	break;

	case LWS_CALLBACK_CLOSED: {

		UserData *userData = (*(static_cast<UserData**>(user)));
		delete userData;

	}	break;


	case LWS_CALLBACK_RECEIVE: {

		Json::Value rcvjson;
		Json::Value respjson;

		Json::CharReaderBuilder builder;
		Json::CharReader *jSonReader = builder.newCharReader();
		std::string errs;

		UserData *userData = (*(static_cast<UserData**>(user)));

		std::string graphRequest;

		graphRequest.append((char*)in, len);

		if(jSonReader->parse(graphRequest.c_str(), graphRequest.c_str() + graphRequest.size(), &rcvjson, &errs)) {		//Verify if we received the whole Json string

			lwsl_info("Json request: %s\n", graphRequest.c_str());

			graphRequest.clear();

			if(rcvjson.isMember("command") && rcvjson["command"].isString()) {

				//Checks if graphis requested
				if(rcvjson["command"] == "get graph") {

					//Check corresponding fields
					if(rcvjson.isMember("id") && rcvjson["id"].isUInt() &&
							rcvjson.isMember("spn") && rcvjson["spn"].isUInt() &&
							rcvjson.isMember("samples") && rcvjson["samples"].isUInt() &&
							rcvjson.isMember("period") && rcvjson["period"].isUInt()
							) {

						if(rcvjson["samples"] > 10000)	rcvjson["samples"] = 10000;		//No more than 10000 samples

						userData->graph = getGraphFromSPN(rcvjson["id"].asUInt(), rcvjson["spn"].asUInt(), rcvjson["samples"].asUInt(), rcvjson["period"].asUInt());
						userData->toSend = true;
						lws_callback_on_writable_all_protocol(lws_get_context(wsi),
								lws_get_protocol(wsi));
					}

				}

			}

		}

		delete jSonReader;

	}	break;

	case LWS_CALLBACK_SERVER_WRITEABLE: {

		UserData *userData = (*(static_cast<UserData**>(user)));

		if(!userData->toSend)	return 0;			//We have to ensure that this callback is not called by the library with this flag.


		userData->toSend = false;
		std::string output;
		userData->graph.SerializeToString(&output);

		char *buff = new char[LWS_SEND_BUFFER_PRE_PADDING + output.size() + LWS_SEND_BUFFER_POST_PADDING];

		memcpy(buff + LWS_SEND_BUFFER_PRE_PADDING, output.c_str(), output.size());

		int written = lws_write(wsi, (unsigned char*)(buff + LWS_SEND_BUFFER_PRE_PADDING),
				output.size(), LWS_WRITE_BINARY);


		delete[] buff;


	}	break;

	default:
		break;
	}

	return 0;

}


Graph getGraphFromSPN(u32 id, u32 spn, u32 number, u32 period) {

	Graph graph;

	//Compose the key for the map. Key = canid + spnNumber
	u64 key = (u64)(id) | ((u64)(spn) << 32);


	auto iter = historyMap.find(key);

	//No history for SPN
	if(iter == historyMap.end())		return graph;


	SPNHistory& history = iter->second;

	//At the moment, only visulize Numeric SPN
	if(history.getNumericSpec() == nullptr)		return graph;


	std::shared_ptr<const SPNNumericSpec> spec = history.getNumericSpec();

	Axis *axisX = graph.mutable_axisx();

	TimeStamp current = TimeStamp::now();
	TimeStamp start = current - period;


	//X axis which is the timestamp in seconds.
	axisX->set_units("s");
	axisX->set_max((double)(current.getSeconds()) +
			(double)(current.getMicroSec()) /1000000);
	axisX->set_min((double)(start.getSeconds()) +
			(double)(start.getMicroSec()) /1000000);

	Axis *axisY = graph.mutable_axisy();

	axisY->set_units(spec->getUnits());
	axisY->set_max(spec->getMaxFormattedValue());
	axisY->set_min(spec->getMinFormattedValue());

	std::vector<SPNHistory::Sample> samples = history.getWindow(current, period, number);
	

	for(auto iter = samples.begin(); iter != samples.end(); ++iter) {


		Graph_Sample *sample = graph.add_samples();

		sample->set_x((double)(iter->getTimeStamp().getSeconds()) +
				(double)(iter->getTimeStamp().getMicroSec()) /1000000);

		sample->set_y(iter->getNumeric());

	}
	


	return graph;

}
