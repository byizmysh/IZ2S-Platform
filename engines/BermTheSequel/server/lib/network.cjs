const os = require("node:os"),
  net = require("node:net");
const port = 39380;
function addresses(interfaces = os.networkInterfaces()) {
  return [
    ...new Set(
      Object.values(interfaces)
        .flat()
        .filter(
          (a) =>
            a.family === "IPv4" &&
            !a.internal &&
            a.netmask !== "255.255.255.255" &&
            !a.address.startsWith("169.254."),
        )
        .map((a) => a.address),
    ),
  ];
}

async function chooseHost(args) {
  if (args.length > 1 || (args.length === 1 && !net.isIPv4(args[0])))
    throw Error("Usage: node server/host.cjs [computer-IP]");
  if (args.length) return args[0];
  const choices = addresses();
  if (choices.length === 1) return choices[0];
  if (!choices.length)
    throw Error("Connect this computer to the console’s local network.");
  if (!process.stdin.isTTY)
    throw Error(
      "Choose the address on the console’s network: node server/host.cjs IP\nAvailable: " +
        choices.join(", "),
    );
  choices.forEach((address, i) => console.log(`  ${i + 1}. ${address}`));
  const input = require("node:readline/promises").createInterface({
    input: process.stdin,
    output: process.stdout,
  });
  try {
    const choice = await input.question(
      "Which computer address can the Switch 2 reach? ",
    );
    if (!/^[1-9][0-9]*$/.test(choice) || !choices[Number(choice) - 1])
      throw Error("Choose one of the listed addresses.");
    return choices[Number(choice) - 1];
  } finally {
    input.close();
  }
}

function connectCommand(host, token, listenPort = port) {
  return `V=START(${[listenPort, ...host.split("."), ...token].join(",")})`;
}

module.exports = { addresses, chooseHost, connectCommand };
