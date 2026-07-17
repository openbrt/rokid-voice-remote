io.stdout:setvbuf("line")

local ROOT = os.getenv("ROKID_VOICE_REMOTE_ROOT") or "/data/rokid-voice-remote"
local COMMANDS = ROOT .. "/config/commands.tsv"
local MANAGED_WORDS = ROOT .. "/state/managed-words"
local MAX_COMMANDS = 5

local native = assert(require("rokidsiren"))
local siren

local function fields(line)
    local values = {}
    for value in string.gmatch(line .. "\t", "([^\t]*)\t") do
        values[#values + 1] = value
    end
    return values
end

local function load_commands(path)
    local commands = {}
    local seen = {}
    for line in io.lines(path) do
        if line ~= "" and string.sub(line, 1, 1) ~= "#" then
            local values = fields(line)
            assert(#values == 6, "commands.tsv requires exactly 6 columns")
            local phrase = values[1]
            local pinyin = values[2]
            assert(phrase ~= "" and pinyin ~= "", "phrase and pinyin are required")
            assert(not seen[phrase], "duplicate phrase: " .. phrase)
            seen[phrase] = true
            commands[#commands + 1] = {phrase = phrase, pinyin = pinyin}
            assert(#commands <= MAX_COMMANDS,
                   "command count exceeds safety limit " .. MAX_COMMANDS)
        end
    end
    assert(#commands > 0, "commands.tsv has no commands")
    return commands
end

local function remove_previous_words(path)
    local file = io.open(path, "r")
    if not file then return end
    for phrase in file:lines() do
        if phrase ~= "" then
            local failed_word = siren:deleteVTWords(phrase)
            if failed_word ~= nil then
                print("VOICE_REMOTE_DELETE_WARNING", tostring(failed_word))
            end
        end
    end
    file:close()
end

local function save_managed_words(path, commands)
    local temporary = path .. ".tmp"
    local file = assert(io.open(temporary, "w"))
    for _, command in ipairs(commands) do
        file:write(command.phrase, "\n")
    end
    file:close()
    assert(os.rename(temporary, path))
end

function onVoiceEvent(id, event, sl, energy)
    if event == native.voiceEvent.AWAKE then
        print("VOICE_REMOTE_AWAKE", tostring(id), tostring(sl), tostring(energy))
        if siren then
            siren:setState(native.state.SLEEP)
        end
    elseif event == native.voiceEvent.CANCEL then
        print("VOICE_REMOTE_CANCEL", tostring(id))
    end
end

local commands = load_commands(COMMANDS)
siren = native()
remove_previous_words(MANAGED_WORDS)

for _, command in ipairs(commands) do
    local result = siren:insertVTWord(
        native.vtType.AWAKE,
        command.phrase,
        command.pinyin,
        false
    )
    assert(result == nil,
           "insertVTWord failed phrase=" .. command.phrase ..
           " result=" .. tostring(result))
end

save_managed_words(MANAGED_WORDS, commands)
siren:setState(native.state.SLEEP)
assert(siren:startStream(), "startStream failed")
print("VOICE_REMOTE_READY commands=" .. tostring(#commands))

return {siren = siren, commands = commands}
