/**
 * TclLinter.js - Tcl syntax checking with stim2-specific knowledge
 * 
 * Checks for:
 * - Basic Tcl syntax (brace/quote balance, unterminated strings)
 * - stim2 graphics commands and common patterns
 * 
 * Command sets can be extended by adding to the appropriate Set.
 */

class TclLinter {
  constructor() {
    this.errors = [];
    this.warnings = [];

    // =========================================================
    // stim2 core commands (from tclproc.c)
    // =========================================================
    
    // Graphics list (glist) commands
    this.glistCommands = new Set([
      'glistInit', 'glistNGroups', 'glistAddObject', 'glistSetEye',
      'glistSetParams', 'glistSetDynamic', 'glistSetFrameInitCmd',
      'glistSetPostFrameCmd', 'glistSetFrameTime', 'glistSetInitCmd',
      'glistSetRepeatMode', 'glistSetSwapMode', 'glistSetVisible',
      'glistSetCurGroup', 'glistGetCurObjects', 'glistGetObjects',
      'glistNextFrame', 'glistOneShotActive', 'glistDump'
    ]);

    // Graphics object (gobj) info commands
    this.gobjCommands = new Set([
      'gobjName', 'gobjType', 'gobjTypeName', 'gobjNameType'
    ]);

    // Object manipulation commands
    this.objCommands = new Set([
      'resetObjList', 'unloadObj', 'nullObj',
      'setVisible', 'setEye', 'translateObj', 'scaleObj', 'rotateObj', 'resetObj',
      'setProjMatrix', 'setObjMatrix', 'useObjMatrix', 'setObjProp',
      'setTranslate', 'setSpin', 'setRotation', 'setSpinRate'
    ]);

    // Script attachment commands
    this.scriptCommands = new Set([
      'addPreScript', 'addPostScript', 'addThisFrameScript', 'addPostFrameScript',
      'activatePreScript', 'activatePostScript', 'activatePostFrameScript',
      'deactivatePreScript', 'deactivatePostScript', 'deactivatePostFrameScript',
      'replacePreScript', 'replacePostScript', 'replacePostFrameScript'
    ]);

    // Animation commands
    this.animationCommands = new Set([
      'toggleAnimation', 'startAnimation', 'stopAnimation', 'kickAnimation'
    ]);

    // Display/window commands
    this.displayCommands = new Set([
      'redraw', 'reshape', 'setBackground', 'setStereoMode',
      'showCursor', 'hideCursor', 'setCursorPos',
      'dumpRaw', 'dumpPS', 'resetGraphicsState'
    ]);

    // Observation list commands
    this.olistCommands = new Set([
      'olistInit', 'olistAddSpec', 'olistDump'
    ]);

    // System/utility commands
    this.systemCommands = new Set([
      'ping', 'quit', 'exit', 'setsystem',
      'setVerboseLevel', 'toggleImgui', 'logMessage',
      'dout', 'dpulse', 'wakeup'
    ]);

    // =========================================================
    // Additional module commands (add as needed)
    // =========================================================
    
    // Display list commands (if using dlsh-style interface)
    this.dlCommands = new Set([
      'dlAddToGroup', 'dlRemoveFromGroup', 'dlClearGroup',
      'clearwin'  // common alias
    ]);

    // Linked variables (read-only awareness)
    this.linkedVars = new Set([
      'StimVersion', 'StimTime', 'StimTicks', 'StimVRetraceCount',
      'NextFrameTime', 'SwapPulse', 'SwapAcknowledge', 'SwapCount',
      'StereoMode', 'BlockMode', 'MouseXPos', 'MouseYPos'
    ]);

    // Combine all known commands for validation
    this.allKnownCommands = new Set([
      ...this.glistCommands,
      ...this.gobjCommands,
      ...this.objCommands,
      ...this.scriptCommands,
      ...this.animationCommands,
      ...this.displayCommands,
      ...this.olistCommands,
      ...this.systemCommands,
      ...this.dlCommands
    ]);
  }

  /**
   * Main lint entry point
   */
  lint(code) {
    this.errors = [];
    this.warnings = [];

    try {
      this.checkBasicSyntax(code);
      this.checkBraceBalance(code);
      this.checkQuoteBalance(code);
      this.checkStim2Patterns(code);

      return {
        isValid: this.errors.length === 0,
        errors: this.errors,
        warnings: this.warnings,
        summary: this.getSummary()
      };
    } catch (error) {
      this.errors.push({
        line: 0,
        column: 0,
        message: `Linter error: ${error.message}`,
        severity: 'error'
      });

      return {
        isValid: false,
        errors: this.errors,
        warnings: this.warnings,
        summary: 'Linting failed'
      };
    }
  }

  /**
   * Check for stim2-specific patterns and common issues
   */
  checkStim2Patterns(code) {
    const lines = code.split('\n');

    lines.forEach((line, lineNum) => {
      const trimmed = line.trim();

      // Skip empty lines and comments
      if (!trimmed || trimmed.startsWith('#')) {
        return;
      }

      // Check for glistSetVisible without redraw nearby
      if (trimmed.includes('glistSetVisible')) {
        let hasRedrawNearby = false;
        for (let i = lineNum; i < Math.min(lineNum + 5, lines.length); i++) {
          if (lines[i].includes('redraw')) {
            hasRedrawNearby = true;
            break;
          }
        }
        if (!hasRedrawNearby) {
          this.warnings.push({
            line: lineNum + 1,
            column: 1,
            message: 'glistSetVisible may need redraw to take effect',
            severity: 'warning'
          });
        }
      }

      // Check for common typos in stim2 commands
      const typoPatterns = [
        { pattern: /\bglistsetvisible\b/i, correct: 'glistSetVisible' },
        { pattern: /\bredrawwin\b/i, correct: 'redraw' },
        { pattern: /\bclearwindow\b/i, correct: 'clearwin' },
      ];

      typoPatterns.forEach(({ pattern, correct }) => {
        const match = trimmed.match(pattern);
        if (match && match[0] !== correct) {
          this.warnings.push({
            line: lineNum + 1,
            column: trimmed.indexOf(match[0]) + 1,
            message: `Did you mean '${correct}'?`,
            severity: 'warning'
          });
        }
      });

      // Warn about modifying linked variables
      this.linkedVars.forEach(varName => {
        const setPattern = new RegExp(`\\bset\\s+${varName}\\b`);
        if (setPattern.test(trimmed)) {
          this.warnings.push({
            line: lineNum + 1,
            column: 1,
            message: `'${varName}' is a linked variable (read-only from Tcl)`,
            severity: 'warning'
          });
        }
      });
    });
  }

  /**
   * Check for basic syntax issues
   */
  checkBasicSyntax(code) {
    const lines = code.split('\n');

    lines.forEach((line, lineNum) => {
      const trimmed = line.trim();

      if (!trimmed || trimmed.startsWith('#')) {
        return;
      }

      if (this.hasUnterminatedString(line)) {
        this.errors.push({
          line: lineNum + 1,
          column: line.length,
          message: 'Unterminated string',
          severity: 'error'
        });
      }
    });
  }

  /**
   * Check brace balance
   */
  checkBraceBalance(code) {
    let braceStack = [];
    let braceCount = 0;
    const lines = code.split('\n');

    lines.forEach((line, lineNum) => {
      let inString = false;
      let escapeNext = false;

      for (let i = 0; i < line.length; i++) {
        const char = line[i];
        const actualLineNum = lineNum + 1;

        if (escapeNext) { escapeNext = false; continue; }
        if (char === '\\') { escapeNext = true; continue; }
        if (char === '"' && !escapeNext) { inString = !inString; continue; }
        if (inString) continue;

        if (char === '{') {
          braceCount++;
          braceStack.push({ line: actualLineNum, column: i + 1, type: 'open' });
        } else if (char === '}') {
          braceCount--;
          if (braceCount < 0) {
            this.errors.push({
              line: actualLineNum,
              column: i + 1,
              message: 'Unmatched closing brace',
              severity: 'error'
            });
            braceCount = 0;
          } else {
            braceStack.pop();
          }
        }
      }
    });

    if (braceCount > 0) {
      const lastOpen = braceStack[braceStack.length - 1];
      this.errors.push({
        line: lastOpen?.line || lines.length,
        column: lastOpen?.column || 1,
        message: `${braceCount} unmatched opening brace${braceCount > 1 ? 's' : ''}`,
        severity: 'error'
      });
    }
  }

  /**
   * Check quote balance
   */
  checkQuoteBalance(code) {
    const lines = code.split('\n');
    let inMultiLineString = false;

    lines.forEach((line, lineNum) => {
      let quoteCount = 0;
      let escapeNext = false;

      for (let char of line) {
        if (escapeNext) { escapeNext = false; continue; }
        if (char === '\\') { escapeNext = true; continue; }
        if (char === '"') quoteCount++;
      }

      if (quoteCount % 2 !== 0) {
        inMultiLineString = !inMultiLineString;
      }
    });

    if (inMultiLineString) {
      this.errors.push({
        line: lines.length,
        column: 1,
        message: 'Unterminated multi-line string',
        severity: 'error'
      });
    }
  }

  /**
   * Check if a line has an unterminated string
   */
  hasUnterminatedString(line) {
    let quoteCount = 0;
    let escapeNext = false;

    for (let char of line) {
      if (escapeNext) { escapeNext = false; continue; }
      if (char === '\\') { escapeNext = true; continue; }
      if (char === '"') quoteCount++;
    }

    return quoteCount % 2 !== 0;
  }

  /**
   * Generate summary string
   */
  getSummary() {
    const errorCount = this.errors.length;
    const warningCount = this.warnings.length;

    if (errorCount === 0 && warningCount === 0) {
      return 'No issues found';
    }

    const parts = [];
    if (errorCount > 0) parts.push(`${errorCount} error${errorCount > 1 ? 's' : ''}`);
    if (warningCount > 0) parts.push(`${warningCount} warning${warningCount > 1 ? 's' : ''}`);

    return parts.join(', ');
  }

  /**
   * Quick validation helper
   */
  static quickValidate(code) {
    const linter = new TclLinter();
    const result = linter.lint(code);
    return result.isValid;
  }

  /**
   * Get just errors helper
   */
  static getErrors(code) {
    const linter = new TclLinter();
    const result = linter.lint(code);
    return result.errors;
  }
}

// Export for browser
window.TclLinter = TclLinter;