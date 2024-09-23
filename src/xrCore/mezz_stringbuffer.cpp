#include "stdafx.h"

#include "mezz_stringbuffer.h"

MezzStringBuffer::MezzStringBuffer(uint32_t Size)
{
	StringBuffer = std::make_unique<char[]>(Size);
	BufferRaw = StringBuffer.get();

	BufferSize = Size;
}

char* MezzStringBuffer::GetBuffer() const
{
	return BufferRaw;
}

uint32_t MezzStringBuffer::GetSize() const
{
	return BufferSize;
}

MezzStringBuffer::operator char* () const
{
	return BufferRaw;
}

// String utils
std::vector<std::string> splitStringMulti(const std::string& inputString, std::string separator, bool includeSeparators, bool trimStrings) {
	std::stringstream stringStream(inputString);
	std::string line;
	std::vector<std::string> wordVector;
	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != std::string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			if (includeSeparators)
				wordVector.push_back(line.substr(pos, 1));

			prev = pos + 1;
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, std::string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			trim(s);
		}
	}
	return wordVector;
}

std::vector<std::string> splitStringLimit(const std::string& inputString, std::string separator, int limit, bool trimStrings) {
	std::stringstream stringStream(inputString);
	std::string line;
	std::vector<std::string> wordVector;
	while (std::getline(stringStream, line))
	{
		std::size_t prev = 0, pos;
		while ((pos = line.find_first_of(separator, prev)) != std::string::npos)
		{
			if (pos > prev)
				wordVector.push_back(line.substr(prev, pos - prev));

			prev = pos + 1;
			if (limit > 0) {
				if (wordVector.size() >= limit) {
					wordVector.push_back(line.substr(prev, std::string::npos));
					return wordVector;
				}
			}
		}
		if (prev < line.length())
			wordVector.push_back(line.substr(prev, std::string::npos));
	}
	if (trimStrings) {
		for (auto& s : wordVector) {
			trim(s);
		}
	}
	return wordVector;
}

std::string getFilename(std::string& s) {
	auto path = splitStringMulti(s, "\\");
	std::string fname = path.empty() ? "" : path.back();
	return fname;
}

void printIniItemLine(const CInifile::Item& s) {
	std::string fname = s.filename.c_str();
	Msg("%s = %s -> %s", s.first.c_str(), s.second.c_str(), fname.c_str());
}

void trim(std::string& s, const char* t) {
	s.erase(s.find_last_not_of(t) + 1);
	s.erase(0, s.find_first_not_of(t));
};
std::string trimCopy(std::string s, const char* t) {
	trim(s, t);
	return s;
}

void toLowerCase(std::string& s) {
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
		return std::tolower(c);
	});
}
std::string toLowerCaseCopy(std::string s) {
	toLowerCase(s);
	return s;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
	if (from.empty())
		return;
	size_t start_pos = 0;
	while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
		str.replace(start_pos, from.length(), to);
		start_pos += to.length(); // In case 'to' contains 'from', like replacing 'x' with 'yx'
	}
}
std::string replaceAllCopy(std::string str, const std::string& from, const std::string& to) {
	replaceAll(str, from, to);
	return str;
}
