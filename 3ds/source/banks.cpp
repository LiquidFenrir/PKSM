/*
 *   This file is part of PKSM
 *   Copyright (C) 2016-2020 Bernardo Giordano, Admiral Fish, piepie62
 *
 *   This program is free software: you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, either version 3 of the License, or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 *   Additional Terms 7.b and 7.c of GPLv3 apply to this file:
 *       * Requiring preservation of specified reasonable legal notices or
 *         author attributions in that material or in the Appropriate Legal
 *         Notices displayed by works containing it.
 *       * Prohibiting misrepresentation of the origin of that material,
 *         or requiring that modified versions of such material be marked in
 *         reasonable ways as different from the original version.
 */

#include "banks.hpp"
#include "Archive.hpp"
#include "Configuration.hpp"
#include "nlohmann/json.hpp"

// Public on purpose: banks being converted need to set their size
nlohmann::json g_banks;

namespace
{
    Result createJson()
    {
        g_banks           = nlohmann::json::object();
        g_banks["pksm_1"] = BANK_DEFAULT_SIZE;
        return Banks::saveJson();
    }

    Result read()
    {
        std::string path = Configuration::getInstance().useExtData() ? "/banks.json" : "/3ds/PKSM/banks.json";
        auto in          = (Configuration::getInstance().useExtData() ? Archive::data() : Archive::sd()).file(path, FS_OPEN_READ);
        if (in)
        {
            size_t size = in->size();
            char data[size + 1];
            in->read(data, size);
            data[size] = '\0';
            in->close();
            g_banks = nlohmann::json::parse(data, nullptr, false);
        }
        else
        {
            return createJson();
        }
        return 0;
    }
}

Result Banks::saveJson()
{
    std::string jsonData = g_banks.dump(2);
    std::string path     = Configuration::getInstance().useExtData() ? "/banks.json" : "/3ds/PKSM/banks.json";
    (Configuration::getInstance().useExtData() ? Archive::data() : Archive::sd()).deleteFile(path);
    (Configuration::getInstance().useExtData() ? Archive::data() : Archive::sd()).createFile(path, 0, jsonData.size());
    auto out = (Configuration::getInstance().useExtData() ? Archive::data() : Archive::sd()).file(path, FS_OPEN_WRITE);
    if (out)
    {
        out->write(jsonData.data(), jsonData.size() + 1);
        out->close();
        return out->result();
    }
    else
    {
        Result ret = out->result();
        out->close();
        return ret;
    }
}

Result Banks::init()
{
    Result res;
    if (R_FAILED(res = read()))
        return res;

    if (g_banks.is_discarded())
        return -1;

    auto i = g_banks.find("pksm_1");
    if (i == g_banks.end())
    {
        i = g_banks.begin();
    }
    loadBank(i.key(), i.value());
    if (bank)
    {
        return 0;
    }
    else
    {
        return -2;
    }
}

bool Banks::loadBank(const std::string& name, const std::optional<int>& maxBoxes)
{
    if (!bank || bank->name() != name)
    {
        auto found = g_banks.find(name);
        if (found == g_banks.end())
        {
            g_banks[name] = maxBoxes.value_or(BANK_DEFAULT_SIZE);
            saveJson();
            found = g_banks.find(name);
        }
        bank = std::make_shared<Bank>(found.key(), found.value().get<int>());
        return true;
    }
    return false;
}

void Banks::removeBank(const std::string& name)
{
    if (g_banks.size() == 1)
    {
        return;
    }
    if (g_banks.contains(name))
    {
        if (bank && bank->name() == name)
        {
            bank   = nullptr;
            auto i = g_banks.begin();
            if (i.key() == name)
            {
                i++;
            }
            loadBank(i.key(), i.value());
        }
        remove(("/3ds/PKSM/banks/" + name + ".bnk").c_str());
        remove(("/3ds/PKSM/banks/" + name + ".json").c_str());
        Archive::data().deleteFile("/banks/" + name + ".bnk");
        Archive::data().deleteFile("/banks/" + name + ".json");
        for (auto i = g_banks.begin(); i != g_banks.end(); i++)
        {
            if (i.key() == name)
            {
                g_banks.erase(i);
                saveJson();
                break;
            }
        }
    }
}

std::vector<std::pair<std::string, int>> Banks::bankNames()
{
    std::vector<std::pair<std::string, int>> ret(g_banks.size());
    for (auto i = g_banks.begin(); i != g_banks.end(); i++)
    {
        ret[std::distance(g_banks.begin(), i)] = {i.key(), i.value().get<int>()};
    }
    return ret;
}

void Banks::renameBank(const std::string& oldName, const std::string& newName)
{
    if (oldName != newName && g_banks.contains(oldName))
    {
        if (bank->name() == oldName)
        {
            if (!bank->setName(newName))
            {
                return;
            }
        }
        else
        {
            Archive::moveFile(Archive::data(), "/banks/" + oldName + ".bnk", Archive::data(), "/banks/" + newName + ".bnk");
            Archive::moveFile(Archive::data(), "/banks/" + oldName + ".json", Archive::data(), "/banks/" + newName + ".json");
            Archive::moveFile(Archive::sd(), "/3ds/PKSM/banks/" + oldName + ".bnk", Archive::sd(), "/3ds/PKSM/banks/" + newName + ".bnk");
            Archive::moveFile(Archive::sd(), "/3ds/PKSM/banks/" + oldName + ".json", Archive::sd(), "/3ds/PKSM/banks/" + newName + ".json");
        }
        g_banks[newName] = g_banks[oldName];
        g_banks.erase(oldName);
        saveJson();
    }
}

void Banks::setBankSize(const std::string& name, int size)
{
    if (g_banks.count(name))
    {
        g_banks[name] = size;
        if (bank && bank->name() == name && size != bank->boxes())
        {
            bank->resize(size);
        }
        saveJson();
    }
}

Result Banks::swapSD(bool toSD)
{
    Result res = 0;
    if (toSD)
    {
        if (R_FAILED(res = Archive::moveDir(Archive::data(), "/banks", Archive::sd(), "/3ds/PKSM/banks")))
            return res;
        if (R_FAILED(res = Archive::moveFile(Archive::data(), "/banks.json", Archive::sd(), "/3ds/PKSM/banks.json")))
            return res;
    }
    else
    {
        if (R_FAILED(res = Archive::moveDir(Archive::sd(), "/3ds/PKSM/banks", Archive::data(), "/banks")))
            return res;
        if (R_FAILED(res = Archive::moveFile(Archive::sd(), "/3ds/PKSM/banks.json", Archive::data(), "/banks.json")))
            return res;
    }
    return res;
}
