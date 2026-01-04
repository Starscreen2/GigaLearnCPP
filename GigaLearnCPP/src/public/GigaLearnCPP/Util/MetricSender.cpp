#include "MetricSender.h"

#include "Timer.h"

namespace py = pybind11;
using namespace GGL;

GGL::MetricSender::MetricSender(std::string _projectName, std::string _groupName, std::string _runName, std::string runID) :
	projectName(_projectName), groupName(_groupName), runName(_runName) {

	RG_LOG("Initializing MetricSender...");

	try {
		pyMod = py::module::import("python_scripts.metric_receiver");
	} catch (std::exception& e) {
		RG_ERR_CLOSE("MetricSender: Failed to import metrics receiver, exception: " << e.what());
	}

	try {
		auto returedRunID = pyMod.attr("init")(PY_EXEC_PATH, projectName, groupName, runName, runID);
		curRunID = returedRunID.cast<std::string>();
		RG_LOG(" > " << (runID.empty() ? "Starting" : "Continuing") << " run with ID : \"" << curRunID << "\"...");

	} catch (std::exception& e) {
		RG_ERR_CLOSE("MetricSender: Failed to initialize in Python, exception: " << e.what());
	}

	RG_LOG(" > MetricSender initalized.");
}

void GGL::MetricSender::Send(const Report& report) {
	py::dict reportDict = {};

	for (auto& pair : report.data)
		reportDict[pair.first.c_str()] = pair.second;

	try {
		pyMod.attr("add_metrics")(reportDict);
	} catch (std::exception& e) {
		// Don't crash training if wandb/metrics fail - just log warning and continue
		// This allows training to continue even if network connection is lost
		static int failureCount = 0;
		failureCount++;
		if (failureCount <= 5) {
			// Log first few failures with details
			RG_LOG("WARNING: MetricSender failed to send metrics (attempt " << failureCount << "): " << e.what());
			RG_LOG("         Training will continue, but metrics may not be logged to wandb.");
		} else if (failureCount == 6) {
			// After 5 failures, stop spamming logs
			RG_LOG("WARNING: MetricSender has failed " << failureCount << " times. Suppressing further warnings.");
			RG_LOG("         Training continues, but metrics are not being logged to wandb.");
		}
		// Continue training even if metrics fail
	}
}

GGL::MetricSender::~MetricSender() {
	
}