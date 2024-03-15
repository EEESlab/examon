module.exports = function (grunt) {

	grunt.loadTasks("../../tasks");

	grunt.initConfig({

		systemjs: {
			es6: {
				options: {
					baseURL: "/",
					configFile: "config.js",
					minify: false,
					sfx: false
				},
				files: {
					"dist/demo.js": "app/init.js"
				}
			}
		}
	});

	grunt.registerTask("default", ["systemjs"]);
};


