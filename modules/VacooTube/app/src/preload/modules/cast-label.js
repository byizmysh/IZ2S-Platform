const os = require('os')
const configOverrides = require('../util/configOverrides')

let hostname = os.hostname() //still the same in flatpak
if (process.platform === 'darwin' && hostname.endsWith('.local')) {
    hostname = hostname.slice(0, -6)
}

module.exports = () => {
    configOverrides.environmentOverrides.push({
        feature_switches: {
            mdx_device_label: `${hostname} (VacooTube)` //label that displays on cast. only mentions VacooTube incase the user doesn't know their hostname
        }
    })
}
