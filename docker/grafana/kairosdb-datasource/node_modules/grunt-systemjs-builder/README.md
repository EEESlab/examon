[![npm version](https://badge.fury.io/js/grunt-systemjs-builder.svg)](https://www.npmjs.com/package/grunt-systemjs-builder)

# grunt-systemjs-builder

## Getting started

### To Install

_npm install --save-dev grunt-systemjs-builder_

### To register for grunt: 

_grunt.loadNpmTasks("grunt-systemjs-builder");_

## To run with grunt use the following command:

_grunt systemjs_

 This is a [MultiTask](http://gruntjs.com/api/grunt.task#grunt.task.registermultitask) therefore it can run using different configuration using targets in the grunt configuration. see [grunt docs](http://gruntjs.com/configuring-tasks#task-configuration-and-targets) for more info.

## Options

In essence, this grunt task simply passes along configuration to the [SystemJS Builder](https://github.com/systemjs/builder). You should familiarize yourself with its configuration [documentation](https://github.com/systemjs/builder/blob/master/README.md).

### baseURL: string

"The baseURL provides a special mechanism for loading modules relative to a standard reference URL."

[systemjs docs](https://github.com/systemjs/systemjs/blob/master/docs/overview.md#baseurl)

_if baseURL is provided through the configuration, it will be the one used and any subsequent **baseURL** configuration (even) from a configFile will be ignored as systemjs only allows to configure baseURL once._

### configFile: String

path to an external configuration file (one with System.config(...) calls).

### sfx: Boolean 
> default: false

Create a [self-executing](https://github.com/systemjs/builder/blob/master/README.md#self-executing-sfx-bundles) bundle

### minify: Boolean
> default: false

shortcut for passing the [build](#buildObject) object with a minify property

### sourceMaps: Boolean
> default: true

shortcut for passing the [build](#buildObject) object with a sourceMapsproperty

### builder: Object

The configuration that is allowed when creating the builder instance. Basically any valid systemjs configuration (same as calling System.config()).
see [builder documentation](https://github.com/systemjs/builder/blob/master/README.md).

<a id="buildObject">
### build: Object

the configuration that is allowed when running build or buildSFX
see [builder documentation](https://github.com/systemjs/builder/blob/master/README.md).

## Files

Configuring the source and destination files is done in the standard grunt way using the "files" property of the target (see example below)

## Example


``` javascript

grunt.initConfig({
	systemjs: {
	            options: {
	                sfx: true,
	                baseURL: "./target",
	                configFile: "./target/config.js",
                	minify: true,
					build: {
					  mangle: false
					}
	            },
	            dist: {
	                files: [{
	                    "src":  "./target/src/init.js",
	                    "dest": "./target/bundles/app.min.js"
	                }]
	            }
	        }
});
```
