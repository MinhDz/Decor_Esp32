const { spawn } = require('child_process');

function callMcpTool(toolName, args = {}) {
  return new Promise((resolve, reject) => {
    const proc = spawn('node', ['C:\\Users\\admin\\Documents\\GitHub\\KiCAD-MCP-Server\\dist\\index.js'], {
      env: {
        ...process.env,
        PYTHONPATH: 'C:\\Program Files\\KiCad\\10.0\\bin\\Lib\\site-packages',
        KICAD_PYTHON: 'C:\\Program Files\\KiCad\\10.0\\bin\\python.exe',
        NODE_ENV: 'production',
        LOG_LEVEL: 'info'
      }
    });

    let buffer = '';
    let readyReceived = false;
    let toolCallSent = false;

    function dispatchToolCall() {
      if (toolCallSent) return;
      toolCallSent = true;
      proc.stdin.write(JSON.stringify({
        jsonrpc: '2.0',
        id: 2,
        method: 'tools/call',
        params: { name: toolName, arguments: args }
      }) + '\n');
    }

    proc.stdout.on('data', d => {
      buffer += d.toString();
      const lines = buffer.split('\n');
      buffer = lines.pop();
      for (const l of lines) {
        if (!l.trim()) continue;
        try {
          const msg = JSON.parse(l);
          if (msg.id === 1) {
            // Send initialized notification
            proc.stdin.write(JSON.stringify({
              jsonrpc: '2.0',
              method: 'notifications/initialized'
            }) + '\n');
            if (readyReceived) {
              dispatchToolCall();
            }
          } else if (msg.id === 2) {
            resolve(msg.result);
            proc.kill();
          }
        } catch (e) {}
      }
    });

    proc.stderr.on('data', d => {
      const errStr = d.toString();
      if (errStr.includes('KiCAD MCP SERVER READY')) {
        readyReceived = true;
        dispatchToolCall();
      }
    });

    // Send initialize
    proc.stdin.write(JSON.stringify({
      jsonrpc: '2.0',
      id: 1,
      method: 'initialize',
      params: { protocolVersion: '2024-11-05', capabilities: {}, clientInfo: { name: 'antigravity-client', version: '1.0' } }
    }) + '\n');

    // Fallback if ready string was already emitted or tool does not need Python
    setTimeout(() => {
      if (!toolCallSent) dispatchToolCall();
    }, 5500);

    setTimeout(() => {
      reject(new Error('KiCad MCP tool call timed out after 25s'));
      proc.kill();
    }, 25000);
  });
}

// CLI usage: node call_kicad_mcp.js <toolName> [jsonArgs]
if (require.main === module) {
  const toolName = process.argv[2];
  if (!toolName) {
    console.error('Usage: node call_kicad_mcp.js <toolName> [jsonArgs]');
    process.exit(1);
  }
  const args = process.argv[3] ? JSON.parse(process.argv[3]) : {};
  callMcpTool(toolName, args)
    .then(res => {
      if (res && res.content && res.content[0] && res.content[0].text) {
        console.log(res.content[0].text);
      } else {
        console.log(JSON.stringify(res, null, 2));
      }
    })
    .catch(err => {
      console.error('Error:', err.message);
      process.exit(1);
    });
}

module.exports = { callMcpTool };

