#include "extension.h"

#include "PyFrameObject.h"
#include "PyDictObject.h"
#include "PyCodeObject.h"
#include "PyInterpreterState.h"
#include "PyThreadState.h"
using namespace PyExt::Remote;

#include "ExtHelpers.h"

#include <engextcpp.hpp>

#include <string>
#include <utility>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iterator>
#include <optional>
using namespace std;


namespace {

	auto getCurrentThreadId(IDebugSystemObjects* pSystem) -> uint64_t
	{
		ULONG currentThreadSystemId = 0;
		HRESULT hr = pSystem->GetCurrentThreadId(&currentThreadSystemId);
		if (FAILED(hr))
			throw runtime_error("Failed to retreive current thread id.");
		return currentThreadSystemId;
	}


	auto getCurrentThreadSystemId(IDebugSystemObjects* pSystem) -> uint64_t
	{
		ULONG currentThreadSystemId = 0;
		HRESULT hr = pSystem->GetCurrentThreadSystemId(&currentThreadSystemId);
		if (FAILED(hr))
			throw runtime_error("Failed to retreive current thread system id.");
		return currentThreadSystemId;
	}


	auto escapeDml(const string& str) -> string
	{
		std::string buffer;
		buffer.reserve(str.size());
		for (auto ch : str) {
			switch (ch) {
				case '&':  buffer += "&amp;";  break;
				case '\"': buffer += "&quot;"; break;
				case '\'': buffer += "&apos;"; break;
				case '<':  buffer += "&lt;";   break;
				case '>':  buffer += "&gt;";   break;
				default:   buffer += ch;       break;
			}
		}
		return buffer;
	}

	auto link(const string& text, const string& cmd, const string& alt = ""s) -> string
	{
		ostringstream oss;
		oss << "<link cmd=\"" << escapeDml(cmd) << "\"";
		if (!alt.empty())
			oss << " alt=\"" << escapeDml(alt) << "\"";
		oss << ">" << escapeDml(text) << "</link>";
		return oss.str();
	}

	auto frameToString(const PyFrameObject& frameObject) -> string
	{
		ostringstream oss;

		auto codeObject = frameObject.code();
		if (codeObject == nullptr)
			throw runtime_error("Warning: PyFrameObject is missing PyCodeObject.");

		auto filename = codeObject->filename();
		oss << "File \"" << link(filename, ".open " + filename, "Open source code.")
			<< "\", line " << frameObject.currentLineNumber()
			<< ", in " << link(codeObject->name(), "!pyobj 0n" + to_string(codeObject->offset()), "Inspect PyCodeObject.");

		return oss.str();
	}


	auto frameToCommandString(const PyFrameObject& frameObject) -> string
	{
		ostringstream oss;

		auto locals = frameObject.locals();
		if (locals != nullptr && locals->offset() != 0)
			oss << link("[Locals]", "!pyobj 0n"s + to_string(locals->offset()), "Inspect this frame's locals.") << " ";

		auto globals = frameObject.globals();
		if (globals != nullptr && globals->offset() != 0)
			oss << link("[Globals]", "!pyobj 0n"s + to_string(globals->offset()), "Inspect this frame's captured globals.") << " ";

		return oss.str();
	}

}

namespace PyExt {

	EXT_COMMAND(pystack, "Prints the Python stack for the current thread, or starting at a provided PyFrameObject", "{;s,o;PyFrameObject address}")
	{
		ensureSymbolsLoaded();

		try {
			unique_ptr<PyFrameObject> frameObject;

			// Either start at the user-provided PyFrameObject, or find the top frame of the current thread, if one exists.
			if (m_NumUnnamedArgs < 1) {
				// Print the thread header.
				auto threadHeader = "Thread " + to_string(getCurrentThreadId(m_System)) + ":";
				Out("%s\n", threadHeader.c_str());

				auto threadSystemId = getCurrentThreadSystemId(m_System);
				auto threadState = PyInterpreterState::findThreadStateBySystemThreadId(threadSystemId);
				if (!threadState.has_value())
					throw runtime_error("Thread does not contain any Python frames.");

				frameObject = threadState->frame();
			} else {
				// Print info about the user-provided PyFrameObject as a header.
				auto frameOffset = evalOffset(GetUnnamedArgStr(0));
				Out("Stack trace starting at (PyFrameObject*)(%y):\n", frameOffset);

				frameObject = make_unique<PyFrameObject>(frameOffset);
			}

			if (frameObject == nullptr)
				throw runtime_error("Could not find PyFrameObject.");

			// Print each frame.
			for (; frameObject != nullptr; frameObject = frameObject->back()) {
				auto frameStr = frameToString(*frameObject);
				Dml("\t%s\n", frameStr.c_str());

				auto frameCommandStr = frameToCommandString(*frameObject);
				Dml("\t\t%s\n", frameCommandStr.c_str());
			}
		} catch (exception& ex) {
			Warn("\t%s\n", ex.what());
			if (!HasFullMemBasic()) {
				Err("\tThe dump file does not contain enough data for PyExt to work properly.\n");
				Err("\tCapture a dump with full memory to ensure PyExt can reconstruct callstacks.\n");
			}
		}

		Out("\n");
	}

}
