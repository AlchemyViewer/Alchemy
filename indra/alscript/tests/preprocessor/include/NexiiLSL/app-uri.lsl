#define agentURI(agent) "secondlife:///app/agent/" + string(agent) + "/inspect"
#define agentAboutURI(agent) "secondlife:///app/agent/" + string(agent) + "/about"
#define regionURI(region) "secondlife:///app/region/" + llEscapeURL(region)
#define objectURI(object) "secondlife:///app/objectim/" + string(object) + "?name=" + llEscapeURL(llKey2Name(object)) + "&owner=" + (string)llGetOwnerKey(object)
#define fullObjectURI(object, name, owner) "secondlife:///app/objectim/" + string(object) + "?name=" + llEscapeURL(name) + "&owner=" + string(owner)
#define groupURI(group) "secondlife:///app/group/" + string(group) + "/inspect"
#define groupAboutURI(group) "secondlife:///app/group/" + string(group) + "/about"
#define experienceURI(experience) "secondlife:///app/experience/" + string(experience) + "/profile"
#define chatURI(channel, message) "secondlife:///app/chat/" + string(channel) + "/" + llEscapeURL(message)
#define wikiLink(link, label) "[" + link + " " + label + "]"
