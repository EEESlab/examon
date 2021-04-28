module.exports = function (grunt) {
	"use strict";

	//todo: allow Arithmetic builds - https://github.com/systemjs/builder#example---arithmetic-expressions
	//todo: use trace api for more advanced builds - https://github.com/systemjs/builder#example---direct-trace-api

	function task() {

		var buildQuickSettings = ["minify", "sourceMaps"],
			Builder = require("systemjs-builder"),
			options = _getOptions.call(this, buildQuickSettings),
			builder = new Builder(options.builder),
			done = this.async();

		if (options.baseURL) {
			grunt.verbose.writeln("systemjs-builder-task - using base url: " + options.baseURL);
			builder.config({baseURL: options.baseURL});
		}

		if (options.configFile) {
			var ignoreBaseUrlInConfFile = !!options.baseURL;
			builder.loadConfig(options.configFile, false, ignoreBaseUrlInConfFile) //load external config file if one specified - instr
				.then(_build.bind(this, builder, options, done))
				.catch(function (err) {
					grunt.fail.fatal(err);
				});
		}
		else {
			_build.call(this, builder, options, done);
		}
	}

	function _isUndefined(val) {
		return typeof val === "undefined";
	}

	function _getOptions(buildQuickSettings) {

		var options = this.options({
			builder: {},
			build: {},
			sfx: false,
			minify: false,
			sourceMaps: true
		});

		buildQuickSettings.forEach(function (setting) {  //make it easier to set build options
			if (_isUndefined(options.build[setting] && !_isUndefined(options[setting]))) {
				options.build[setting] = options[setting];
			}
		});

		return options;
	}

	function _build(builder, options, done) {

		var buildMethod = options.sfx ? "buildStatic" : "bundle",
			counter = this.files.length,
			data = {
				builder: builder,
				buildMethod: buildMethod,
				options: options
			};

		grunt.verbose.writeln("systemjs-builder-task - running build method: " + buildMethod);

		this.files.forEach(_buildSource.bind(this, data, function () {
			counter -= 1;

			if (counter === 0) {
				done();
			}
		}));
	}

	function _buildSource(data, done, file) {

		if (file.src.length > 1) { //todo: support multiple src files using "&" - https://github.com/systemjs/builder#example---common-bundles
			grunt.fail.fatal("systemjs-builder-task - cant have more than one source file for the build process");
		}

		var builder = data.builder,
			options = data.options,
			src = file.src[0] || file.orig.src[0]; //falls back the this original string which may include arithmetic. if not it'll fail also

		grunt.verbose.writeln("systemjs-builder-task - about to build source: " + src);

		builder[data.buildMethod].call(builder, src, file.dest, options.build)
			.then(function () {
				grunt.verbose.writeln("systemjs-builder-task - finished building source: " + src);
				done();
			})
			.catch(function (err) {
				grunt.fail.fatal(err);
			});
	}

	grunt.registerMultiTask("systemjs", "build project using systemjs builder", task);
};